# 内存、调度、中断

现有的 `boot.S` 停在长模式入口 `entry64`。接下来的内核只做三件事：管理物理页、在几个内核任务之间轮转、用时钟中断推动这次轮转。

引导阶段已经用 2MB 页恒等映射了低 1GB。QEMU 默认内存是 128MB，落在这张映射里面。所以第一版内存初始化只做物理页分配，不再建新页表。虚拟地址等于物理地址。

Linux 6.10 里对应的是三条更长的链：`e820` 到伙伴系统，`sched_init()` 到 `pick_eevdf()`，`idt_setup_early_handler()` 到 `init_IRQ()` 和时钟。这里每条链只留一个能跑起来的最小形态。

## 启动顺序

中断在任务和中断门都准备好之前保持关闭。`boot.S` 里的 `cli` 一直有效，直到 `kmain` 的最后才 `sti`。

```text
entry64
  保存 Multiboot 信息指针
  pmm_init          物理页位图
  task_init         任务表，此时还没有可运行任务
  idt_init          异常和 IRQ0 的门
  pic_init          把 IRQ 挪到 32 号向量
  pit_init          约 100Hz
  task_create 两个任务
  sti
  scheduler         不再返回
```

`cpuid` 会覆盖 `%ebx`，而 Multiboot 信息指针就在 `%ebx`。`_start` 里必须在第一条 `cpuid` 之前把 `%ebx` 存进全局变量。魔数 `%eax == 0x2BADB002` 也一并保存，供 `pmm_init` 检查。

## 三者怎么配合

```text
时钟 IRQ0
  -> 中断桩保存现场，向 PIC 发 EOI
  -> 当前任务 yield，swtch 到调度器栈
  -> 调度器按轮转挑下一个 READY 任务
  -> swtch 到那个任务自己的栈
  -> 从它上次停下的地方继续
```

内存为每个任务提供一页内核栈。调度器决定谁占用 CPU。中断是唯一的抢占点。页错误在这一版里表示内核有 bug，处理程序打印向量后停机，不实现按需分页。

调度器自己没有任务结构。启动栈 `stack_top` 就是调度器栈。任务运行时用自己的页。时钟若在调度器里响起，当前任务指针为空，处理程序只发 EOI 就返回，避免在调度器栈上再次切换。

## 内存初始化

### 物理内存范围

Multiboot 信息结构的前三个字段：

| 偏移 | 字段 | 含义 |
| --- | --- | --- |
| 0 | `flags` | 第 0 位为 1 时，下面两项有效 |
| 4 | `mem_lower` | 低 1MB 里可用的 KB 数，本设计不使用 |
| 8 | `mem_upper` | 1MB 以上连续可用内存的 KB 数 |

可用物理内存的结尾：

```text
end = min(1MB + mem_upper * 1024, 1GB)
```

上限取 1GB，是因为引导页表只映射了这么多。`flags` 第 0 位无效时，退回到 128MB，和 QEMU 的默认内存一致。

Linux 从 `boot_params.e820_table` 读取不连续的内存区间。这里用 Multiboot 的一个连续上限代替那张表。

### 位图

一页 4KB，一位表示一页。128MB 是 32768 页，位图 4KB。静态放在 `.bss`，按最大 256MB 预留 8KB，避免分配器自己依赖分配器。

`pmm_init` 先把全部位标成已用，再把 `[内核占用结束, end)` 标成空闲。内核占用是 `[0, stack_top)`，里面有引导代码、GDT、三张引导页表和启动栈。低于 1MB 的区域保持已用，固件和 VGA 文本缓冲 `0xb8000` 在这里。

```text
void     pmm_init(uint32_t multiboot_info);
uint64_t pmm_alloc(void);   /* 返回 4KB 对齐的物理地址，没有页时返回 0 */
void     pmm_free(uint64_t phys);
```

分配从低地址向高地址找第一个 0 位。释放前检查地址在 `end` 之内且 4KB 对齐，然后清位。不做伙伴系统，也不合并连续页。

Linux 的 `mm_core_init()` 最终会走到 `mem_init()` 和 `kmem_cache_init()`。这里的位图只承担「把一页交给调用者」这一步。

## 调度

### 任务

最多 8 个任务，静态数组，不从页分配器里拿任务结构本身。每个任务的栈是 `pmm_alloc()` 得到的一页，栈顶在页的高地址。

```text
enum { UNUSED, READY, RUNNING };

struct context {
    uint64_t r15, r14, r13, r12, rbx, rbp;
    uint64_t rip;
};

struct task {
    int state;
    struct context ctx;
    uint8_t *stack_page;
};

struct task tasks[8];
struct task *current;     /* 在调度器栈上时为 NULL */
int rr_index;
struct context sched_ctx;
```

`context` 的字段顺序必须和 `swtch` 的出栈顺序一致。`swtch` 只保存被调用者保存的寄存器。调用者保存的寄存器留在调用 `swtch` 的那一侧。

轮转就是从 `rr_index` 的下一项开始扫描数组，找到第一个 `READY`。扫描一圈都没有，调度器执行 `sti; hlt; cli`，等下一次时钟再扫。

### 切换

```text
swtch(struct context *save, struct context *load);
```

`save` 记下当前栈，`load` 换成目标栈，然后 `ret`。`ret` 弹出的地址是 `context.rip`。

新任务创建时，在它的栈顶放好一份 `context`，`rip` 指向一个小的汇编入口。那个入口把函数指针和参数从栈上取出，调用类似下面的函数：

```text
void thread_a(void) {
    for (;;)
        serial_putc('A');
}
```

`task_create` 失败只发生在没有空槽或没有空闲页的时候，直接停机即可。

`yield` 把当前任务改回 `READY`，再 `swtch` 回 `sched_ctx`。调度器被切回来后把 `current` 清掉，继续挑下一个。

任务函数不调用 `yield`。抢占全部来自时钟中断。这样队列只在中断已经关闭的路径上被修改。

Linux 6.10 用 `task_struct`、每 CPU 运行队列和 `pick_eevdf()` 做同一件事。这里用固定数组和递增下标代替。

## 中断

### 门

长模式下每个 IDT 项 16 字节：

```text
偏移 0..15
代码段选择子 0x08
IST = 0，类型 = 0x8E（中断门，关中断后进入）
偏移 16..63
```

IDT 共 256 项，静态分配。`lidt` 在 `sti` 之前执行。

CPU 异常 0 到 31 各有一个桩。8、10、11、12、13、14、17、21 号异常由 CPU 压入错误码，其余桩补一个 0。所有桩再压入向量号，跳到同一个 `trap_common`。

`trap_common` 保存通用寄存器，调用 C 函数。异常处理只通过串口打印向量号和错误码，然后 `hlt`。14 号页错误可以额外打印 `CR2`。

IRQ 由 8259 PIC 接入。初始化时把主片的基向量设为 32，从片设为 40，免得和 CPU 异常重叠。然后屏蔽从片以及主片上除 IRQ0 以外的线。中断结束时向端口 `0x20` 写 `0x20`。

### 时钟

PIT 通道 0，模式 3。输入频率 1193182Hz，除数取 11932，大约 100Hz。向量 32 的桩：

```text
保存寄存器
给 PIC 发 EOI
若 current != NULL，调用 yield
恢复寄存器
iretq
```

`yield` 切换出去时，中断帧留在该任务的栈上。这个任务下次被 `swtch` 回来，才从 `yield` 返回，再执行 `iretq`。`iretq` 恢复的 `RFLAGS` 里中断标志是 1，所以任务会继续被时钟打断。

`swtch` 不保存 `RFLAGS`。第一次进入任务之前必须已经 `sti`，新任务沿用调度器当时可屏蔽中断已打开的状态。

Linux 在 `start_kernel()` 里先 `sched_init()`，再 `init_IRQ()` 和 `time_init()`，然后才 `local_irq_enable()`。上面的 `kmain` 顺序与此相同。

## 建议拆成的文件

| 文件 | 内容 |
| --- | --- |
| `boot.S` | 保留现有模式切换；保存 Multiboot 指针；`entry64` 改为调用 `kmain` |
| `kmain.c` | 按上面的顺序初始化，最后进入 `scheduler` |
| `pmm.c` | 位图分配器 |
| `task.c` | 任务表、`task_create`、`yield`、`scheduler` |
| `swtch.S` | 保存和恢复 `context` |
| `trap.S` | 异常桩、IRQ0 桩、`trap_common` |
| `idt.c` | 填 IDT 并 `lidt` |
| `pic.c` | PIC 重映射、屏蔽、EOI |
| `serial.c` | 从 `boot.S` 移出的 `0x3f8` 输出，供 C 使用 |
| `linker.ld` | 继续使用现在的 1MB 布局和启动栈 |

`Makefile` 把这些 C 文件用 `-m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -nostdlib` 编译。`-mno-red-zone` 是必须的，中断会使用当前栈顶以下的区域。

## 怎样算做成

`make run` 的串口输出里，`A` 和 `B` 交替出现。两个任务都是死循环，中间没有主动让出 CPU。能交替，就说明时钟中断进得来，`yield` 换了栈，调度器也换了人。

内存分配的直接证据是两个任务都成功拿到了栈页。把可用内存故意标成只剩一页时，第二个 `task_create` 应当停机。这个检查可以后做，不影响主路径。

## 这一版不包含

用户态、系统调用、多核、优先级、睡眠队列、缺页换入、内核堆对象分配、APIC 和 IOAPIC。需要小块内存时，继续用静态数组。任务栈是这一版唯一从 `pmm_alloc` 取走的页。
