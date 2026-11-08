# 从 32 位保护模式进入 64 位，并跑起一个菜单

这个目录是一个可以编译的最小内核。磁盘启动时，`mbr.S` 在实模式里把内核读到 1MB，打开保护模式后跳到 `boot.S` 的 `_start`。`_start` 再打开长模式，`kmain` 画出菜单。菜单按键调用 `user/` 里的 `help_main`、`thread_a_main`、`thread_b_main`，或关机。这三份代码链进同一个 `boot.elf`，和菜单共用启动栈，直接调用 `vga_putc`、`serial_putc`、`kbd_poll`。屏幕写 VGA 文字，串口同时写 COM1。

设计草稿在 `md/DESIGN.md`。用户程序的放法写在 `md/FS.md` 开头。安装和写入 U 盘在 `md/INSTALL.md`。任务切换的实现和 `md/DESIGN.md` 有一处不同：任务不保存单独的 `context` 结构，`swtch` 把被调用者保存的寄存器直接压在当前栈上，任务只记录 `rsp`。当前菜单直接调用 `user/` 里的函数，没有进入 `scheduler`。

它对照 Linux 6.10 压缩内核 `arch/x86/boot/compressed/head_64.S` 里 `startup_32` 的模式切换，从打开 PAE 到进入 64 位入口。内核解压、KASLR、5 级页表都不在这里。实模式和 A20 在 `mbr.S`，不在 `boot.S`。后面的页分配、轮转和时钟也只是能跑起来的最小形态，不对应 `start_kernel()` 的其余部分。

## 建议的阅读顺序

1. `mbr.S`：实模式读盘、打开 A20、进入保护模式、跳到 `_start`。
2. `linker.ld`：程序放在物理地址 1MB，页表和启动栈在映像末尾。
3. `boot.S`：清 `.bss`、GDT、1GB 恒等映射、四条控制寄存器指令、远跳转到 `entry64`。
4. `kmain.c`：初始化顺序，`items` 表，然后进入菜单。
5. `user/help.c`、`user/thread_a.c`、`user/thread_b.c`：菜单里运行的三份代码。
6. `vga.c`、`kbd.c`、`serial.c`：屏幕、键盘和串口。
7. `pmm.c`：一位一页，只分配任务栈。
8. `task.c` 和 `swtch.S`：任务表和换栈。菜单目前不调用 `scheduler`。
9. `pic.c`、`idt.c`、`trap.S`：时钟中断和异常。

## 文件

| 文件 | 作用 |
| --- | --- |
| `mbr.S` | 磁盘第一个扇区。读内核、打开 A20、进入保护模式 |
| `mbr.ld` | 引导扇区运行在 `0x7C00`，文件从偏移 0 开始 |
| `boot.S` | 清 `.bss`、GDT、页表、32 位到 64 位 |
| `linker.ld` | 链接到 1MB，留出三页页表和 4KB 启动栈 |
| `kernel.h` | 各文件之间的函数、三个菜单程序和两个引导变量 |
| `kmain.c` | 初始化后进入菜单。`items` 写着按键、标题和函数 |
| `user/help.c` | 按键 `1`，打印帮助，按 `q` 返回 |
| `user/thread_a.c` | 按键 `2`，连续输出 `A`，按 `q` 返回 |
| `user/thread_b.c` | 按键 `3`，连续输出 `B`，按 `q` 返回 |
| `vga.c` | 写 `0xB8000` 的 80×25 文字 |
| `kbd.c` | 查询 PS/2 端口 `0x60` |
| `pmm.c` | 物理页位图 |
| `task.c` | 最多 8 个任务、轮转、`scheduler` |
| `swtch.S` | 换栈；新任务从 `task_entry` 进入 |
| `idt.c` | 256 项 IDT，`lidt` |
| `trap.S` | 异常 0–31 的桩，以及 IRQ0 |
| `pic.c` | 8259 重映射、屏蔽，PIT 约 100Hz |
| `serial.c` | COM1 `0x3f8` 输出和 `panic` |
| `Makefile` | 编译、检查 Multiboot 头、做磁盘镜像、启动 |

## 编译和运行

在本目录执行：

```bash
make
make check
make boot
```

`make` 生成 `boot.elf`。C 和汇编都用 `gcc -m64` 编译，链接用 `ld -m elf_x86_64`。`boot.S` 开头有 `.code32`，汇编器仍按 32 位编码发出 `_start` 里的指令；`.code64` 之后才是 64 位指令。用 `objdump -d` 看 `boot.elf` 时，32 位那一段会被误判成 64 位指令，要以 `boot.S` 和机器码为准。

`make check` 执行 `grub-file --is-x86-multiboot boot.elf`。成功时没有输出，退出码为 0。这只说明 ELF 里还有 Multiboot 头，磁盘启动不读这个头。

`make boot` 先做 `myos.img`，再从这块硬盘启动，不使用 `-kernel`：

```bash
objcopy -O binary boot.elf kernel.bin
qemu-system-x86_64 -drive file=myos.img,format=raw,if=ide -display curses -serial file:serial.log -no-reboot
```

`myos.img` 的 LBA 0 是 `mbr.bin`，LBA 1 起是 `kernel.bin`。引导扇区把内核拷到 `0x100000`。

`make run` 仍走 QEMU 的 `-kernel`。它先把 `boot.elf` 转成 32 位 ELF `boot32.elf`。QEMU 不接受 64 位 ELF，直接把 `boot.elf` 交给它会报 `Cannot load x86-64 image, give a 32bit one.` 转换只改容器，`entry64` 之后的机器码仍是 64 位。

启动后屏幕是菜单，不是自动刷字母：

```text
menu
1  help
2  run thread_a
3  run thread_b
4  power off
```

前三行来自 `kmain.c` 里的 `items`。`4  power off` 写在 `show_menu` 里，因为它要写关机端口。按 `1` 显示帮助页。按 `2` 或 `3` 后，对应函数一直写 `A` 或 `B`。这三份代码里按 `q` 就返回，菜单再画一次。按 `4` 向端口 `0x604` 写 `0x2000`，QEMU 退出。curses 占用当前终端，串口写到 `serial.log`。Ctrl-C 不会结束 QEMU。按 `4`，或按 Esc 再按 2，进入监视器后输入 `quit`。

### GTK 窗口起不来

去掉 `-display none` 后，QEMU 默认打开 GTK 图形窗口，用来显示写进 `0xB8000` 的文字。这台机器没有图形界面，`DISPLAY` 为空，于是：

```text
gtk initialization failed
make: *** [Makefile:39: run] Error 1
```

`-display curses` 不依赖 GTK。它用终端把 80×25 文字模式画出来，`vga_putc` 写入的字符可以直接看到。curses 已经占用了这个终端，串口不能再用 `-serial stdio`，否则两路输出抢同一个屏幕。COM1 改写到 `serial.log`，需要看串口时另开文件。

屏幕只显示单字节字符。中文的 UTF-8 写进 `0xB8000` 会变成乱码，菜单文字因此用 ASCII。

CPU 不支持长模式时，屏幕左上角是红底白字 `NO`，串口没有输出。QEMU 的虚拟 CPU 支持长模式，这条路径是给真机准备的。写入 U 盘的步骤在 `md/INSTALL.md`。

清理：

```bash
make clean
```

## 指令从哪条开始执行

从磁盘启动时，CPU 的第一条指令在 `mbr.S` 的 `_start`，地址是 `0x7C00`，还是 16 位实模式。它打开 A20，把 `kernel.bin` 读到 `0x100000`，进入 32 位保护模式，再跳到内核的 `_start`。

`make run` 没有这段扇区程序。QEMU 的 `-kernel` 已经处在 32 位保护模式，直接跳到 `boot.S` 的 `_start`。

长模式切换完成之后，才 `call kmain`。`boot.S` 里的 Multiboot 头是数据，`no_long_mode` 在支持长模式时不会走到。

`_start` 先清 `.bss`，再保存 EAX、EBX，装上 GDT，检查长模式，建好低 1GB 页表，然后按固定顺序写 CR4、CR3、EFER、CR0。远跳到 `entry64` 之后，把段寄存器和栈准备好，再调用 C 函数：

```text
ljmpl $SEL_CODE64, $entry64

entry64:
    xorl %eax, %eax
    movl %eax, %ds
    movl %eax, %es
    movl %eax, %ss
    movl $stack_top, %esp
    call kmain
```

`entry64` 仍在 `boot.S` 里，已经是 64 位代码。它把栈设到 `stack_top`，然后调用 `kmain`。

`kmain` 里的顺序是：物理页、任务表、IDT、PIC、PIT，开中断，进入 `menu`。`menu` 不返回，所以 `call kmain` 后面的 `hlt` 循环不会执行。

按 `1`、`2`、`3` 时，菜单调用 `items` 里的 `help_main`、`thread_a_main` 或 `thread_b_main`。按 `q` 后函数返回，菜单再画一次。`task_create` 和 `scheduler` 仍在 `task.c` 里，这条菜单路径没有用到它们。

## 从加载到菜单

```text
BIOS
  把 LBA 0 载入 0x7C00，实模式
        |
        v
mbr.S
  打开 A20
  从 LBA 1 把 kernel.bin 拷到 0x100000
  CR0.PE = 1，跳到内核 _start
        |
        v
boot.S _start
  已在 32 位保护模式，分页关闭
  清 .bss，保存 EAX / EBX
  装上 GDT，数据段 0x10，ESP = stack_top
  CPUID 确认支持长模式
        |
        v
清三页页表，做成低 1GB 恒等映射
CR4.PAE -> CR3 -> EFER.LME -> CR0.PG
ljmpl 0x08:entry64
        |
        v
entry64
  DS/ES/SS 清成空选择子，调用 kmain
        |
        v
kmain
  pmm_init -> task_init -> idt_init -> pic_init -> pit_init
  sti
  menu
    1 help  2 thread_a_main  3 thread_b_main  4 关机
    程序里按 q 返回菜单
```

`make run` 从 `_start` 开始，没有前面的 BIOS 和 `mbr.S`。那时 EAX 是 Multiboot 魔数 `0x2BADB002`。磁盘启动时 EAX 不是这个魔数，`pmm_init` 按 128MB 计算内存。

`boot.S` 里的 `cli` 一直有效，直到 `kmain` 进入菜单之前才 `sti`。页表和 IDT 都在中断打开之前准备好。

## 映像布局

`linker.ld` 把虚拟地址和物理地址都定在 1MB，也就是 `0x100000`。这和 Linux 引导头里 `code32_start` 的默认值相同。低 1MB 留给固件和 VGA 文本缓冲 `0xb8000`。

当前这次链接的结果如下。改代码后地址会变，符号名不变。

| 地址 | 符号 | 内容 |
| --- | --- | --- |
| `0x100000` | `kmain` | `.text` 起点，菜单入口 |
| `0x100954` | `help_main` | 帮助页 |
| `0x1009e3` | `thread_a_main` | 连续输出 `A` |
| `0x100a24` | `thread_b_main` | 连续输出 `B` |
| `0x100a68` | `multiboot_header` | 12 字节 Multiboot 头 |
| `0x100a74` | `_start` | 32 位入口 |
| `0x100b56` | `entry64` | 64 位入口 |
| `0x100b6b` | `swtch` | 换栈 |
| `0x100b86` | `task_entry` | 新任务第一次被调度时进入 |
| `0x100c6b` | `irq_timer` | 时钟中断 |
| `0x100d90` | `gdt` | 空描述符、64 位代码、数据段 |
| `0x100fd5` | `multiboot_magic` | 保存的 EAX |
| `0x100fe0` | `__bss_start` | `.bss` 起点，`_start` 从这里清到 `_end` |
| `0x101000` | `bitmap` | 8KB，覆盖最多 256MB |
| `0x105000` | `pml4` | 页表第一页 |
| `0x106000` | `pdpt` | 页表第二页 |
| `0x107000` | `pd` | 页表第三页，512 个 2MB 项 |
| `0x108000` | `stack_bottom` | 启动栈，4KB |
| `0x109000` | `stack_top` | 启动栈顶，也是 `_end` |

页表和栈不占文件空间，它们在 `.bss` 里用符号把当前位置向前拨。`stack_top` 之后的物理页才交给 `pmm_alloc`。

Multiboot 头不必位于文件第一个字节，但必须 4 字节对齐，并且完整落在 ELF 文件的前 8192 字节里。链接脚本用了 `-z max-page-size=0x1000`。x86-64 链接器默认按 2MB 对齐装载段，那样头会被推到 8192 字节之外，QEMU 的 `-kernel` 就认不出这个映像。磁盘启动不读这个头，它按 `readelf` 里的入口地址跳到 `_start`。

## `boot.S`：保护模式到长模式

### Multiboot 头

```text
magic     0x1BADB002
flags     0
checksum  -magic
```

三项之和为 0。`flags` 为 0，表示不向引导程序提额外要求。引导程序进入 `_start` 时，EAX 是另一个魔数 `0x2BADB002`，EBX 指向信息结构。这两个数要在第一条 `cpuid` 之前写进 `multiboot_magic` 和 `multiboot_info_phys`，因为 `cpuid` 会覆盖 EAX、EBX、ECX、EDX。

### GDT

远跳转之前必须先 `lgdt`。表里三项：

| 选择子 | 描述符 | 含义 |
| --- | --- | --- |
| `0x00` | 全 0 | 空描述符 |
| `0x08` | `0x00af9a000000ffff` | 64 位代码，`L=1`，`D=0`，`P=1`，可读 |
| `0x10` | `0x00cf92000000ffff` | 32 位数据段，基址 0，4GB，可写 |

`_start` 把 DS、ES、SS 都设成 `0x10`，ESP 设成 `stack_top`。此时还是 32 位，栈在映像末尾那 4KB 里。

长模式要求代码段 `L=1` 且 `D=0`。`D=1` 同时 `L=1` 是保留组合，CPU 不会把它当成 64 位代码段。

### 是否支持长模式

`cpuid` 叶 `0x80000000` 的返回值必须不小于 `0x80000001`，然后叶 `0x80000001` 的 EDX 第 29 位必须为 1。否则写 VGA：

```text
0xb8000  0x4f4e    红底白字 N
0xb8002  0x4f4f    红底白字 O
```

然后 `hlt` 死循环。这条路径不使用串口，因为串口初始化不在 `boot.S` 里。

### 低 1GB 恒等映射

三张表各 4KB，先用 `rep stosl` 清零：

```text
PML4[0] = pdpt  | Present | Writable     低 32 位是 3
PDPT[0] = pd    | Present | Writable
PD[i]   = i * 2MB | Present | Writable | PS
```

`PD` 有 512 项，512 乘 2MB 等于 1GB。循环里每次给项的低 32 位加上 `0x200000`，高 32 位保持清零时的 0。虚拟地址等于物理地址，所以打开分页后，正在执行的指令地址仍然落在同一条指令的下一个字节上。

这里的上级表项只有 Present 和 Writable。Linux 同一位置写下的低 32 位是 `0x1007`：下一级表刚好在 4KB 之后，标志里还有 User。2MB 项 Linux 写下 `0x183`，比这里多了 Accessed。切换本身不需要这两位。

1GB 盖住了本程序、启动栈、VGA，也盖住 QEMU 默认的 128MB 内存。所以后面不再建新页表。

### 四条指令的顺序

长模式要四件事同时成立：PAE、一张 4 级页表、`EFER.LME`、`CR0.PG`。

1. `CR4` 第 5 位置 1，打开 PAE。
2. 把 `pml4` 的物理地址写入 `CR3`。
3. 读 `MSR 0xC0000080`，把第 8 位 `LME` 置 1，再写回。
4. 把 `CR0` 写成 `0x80050033`。

`0x80050033` 是这些位的或：

| 位 | 名字 | 作用 |
| --- | --- | --- |
| 0 | PE | 保持保护模式 |
| 1 | MP | 监视协处理器 |
| 4 | ET | 扩展类型，i386 之后的固定位 |
| 5 | NE | 数值异常用异常 16，不用外部中断 |
| 16 | WP | 超级用户也不能写只读页 |
| 18 | AM | 允许对齐检查 |
| 31 | PG | 打开分页 |

这就是 Linux `arch/x86/include/uapi/asm/processor-flags.h` 里的 `CR0_STATE`。

写完 `CR0.PG` 时，CPU 已经按长模式使用页表，但当前 CS 仍是进入 `_start` 时的 32 位代码段，指令仍按 32 位译码。这个状态叫兼容模式。下一条必须是：

```text
ljmpl $0x08, $entry64
```

选择子 `0x08` 的描述符 `L=1`、`D=0`，CPU 从 `entry64` 开始按 64 位指令译码。Linux 在 `head_64.S` 里用 `push` 加 `lret` 做同一次跳转。

### `entry64`

把 EAX 清零，再写入 DS、ES、SS。64 位寻址不再使用这些段的基址，空选择子即可。ESP 再次设成 `stack_top`，然后 `call kmain`。`menu` 不返回。万一 `kmain` 返回了，后面是 `hlt` 死循环。

## `kmain`：初始化顺序

```text
pmm_init()
task_init()
idt_init()
pic_init()
pit_init()
sti
menu()
```

`menu` 按 `items` 选择帮助、`thread_a_main`、`thread_b_main` 或关机。三个函数都在 `user/` 里，链进内核后直接调用 `vga_puts`、`serial_putc` 和 `kbd_poll`。按下 `q` 就从函数返回。

`task.c` 里的 `scheduler` 仍按时钟中断轮转任务，菜单这条路径没有调用它。

这个顺序和 Linux `start_kernel()` 的前半段同一方向：先有任务结构，再装中断和时钟，最后才开中断。Linux 后面还有 `mm_init`、`rest_init`、pid 1 和 `kthreadd`，这里没有。

## 物理页

引导页表已经恒等映射了低 1GB，分配器只记录哪一页空闲。一页 4KB，位图里 1 表示占用。

`pmm_init` 先假定机器有 128MB，和 QEMU 的默认内存一致。然后检查引导时保存的两个值：

- `multiboot_magic == 0x2BADB002`
- `multiboot_info_phys != 0`
- 信息结构偏移 0 的 `flags` 第 0 位为 1

三者都成立时，读取偏移 8 的 `mem_upper`。它的单位是 KB，表示 1MB 以上还有多少内存：

```text
end = 1MB + mem_upper * 1024
```

`end` 再被截到 1GB，因为页表只映射了这么多，然后按 4KB 向下对齐。

位图静态放在 `.bss`，长度 8192 字节，对应 256MB。先全部写成 `0xff`，再从 `stack_top` 向上把 `[stack_top, end)` 里的页清成空闲。`stack_top` 之前保持占用，里面有代码、GDT、位图、三张页表、启动栈，以及没有映射进内核映像的低 1MB。低 1MB 里有 VGA 文本缓冲，不能分出去。

`pmm_alloc` 从第 0 帧向高地址找第一个 0 位，找到就置 1 并返回页的物理地址，找不到返回 0。`pmm_free` 要求地址小于 `end` 且 4KB 对齐，然后清位。没有伙伴系统，也不合并相邻页。这一版唯一的分配者是 `task_create`，每个任务一页栈。

位图只覆盖 256MB。QEMU 默认 128MB，`frame_count` 落在位图里面。不要用大于 256MB 的 `-m`，否则分配循环会按 `end` 扫出位图。

## 任务

最多 8 个槽，静态数组，任务结构本身不向页分配器申请。

```text
UNUSED   槽还没用
READY    可以被挑中
RUNNING  正在占用 CPU，或者已经让出、等待被切回来
```

`current` 指向正在任务栈上运行的那个任务。调度器使用启动栈 `stack_top`，这时 `current` 是空指针。

### 新任务的栈

`task_create` 取一页，栈从这一页的高地址向下长。`swtch` 弹出的顺序是 `r15、r14、r13、r12、rbx、rbp`，然后 `ret`。所以创建时从高地址到低地址写成：

```text
页尾
  fn            线程函数
  task_entry    第一次 ret 的目标
  rbp = 0
  rbx = 0
  r12 = 0
  r13 = 0
  r14 = 0
  r15 = 0
rsp 在这里
```

第一次被 `swtch` 进来时，六个寄存器弹成 0，`ret` 到 `task_entry`。`task_entry` 执行 `sti`，再弹出 `fn` 并 `call`。函数如果返回，就 `hlt` 停住。菜单里的 `thread_a_main` 和 `thread_b_main` 不走这条路径，它们由 `menu` 直接调用，按 `q` 后 `return`。

`sti` 放在 `task_entry` 里，是因为 `swtch` 不保存 `RFLAGS`。调度器在切出去之前执行了 `cli`。老任务被切回来时，要靠自己栈上的 `iretq` 恢复中断标志；新任务没有那一帧，所以由 `task_entry` 打开中断。

页尾按 4KB 对齐，也就按 16 字节对齐。上面压了 8 个 8 字节槽。`swtch` 弹出 6 个寄存器并 `ret` 之后，`task_entry` 再弹出函数指针，`call` 进入线程函数时 `RSP ≡ 8 (mod 16)`，符合 System V ABI。

### 轮转

`pick_next` 从 `rr_index` 的下一项开始绕数组一圈，找第一个 `READY`。下标按 `NTASK - 1` 做与运算，所以任务数必须是 2 的幂，现在是 8。

`scheduler`：

```text
挑到任务:
    cli
    state = RUNNING
    current = 这个任务
    swtch(保存到 sched_rsp，换成任务的 rsp)
    current = 0
    再挑

一个 READY 都没有:
    sti
    hlt
    cli
    再挑
```

`cli` 放在写 `current` 和 `swtch` 的紧前面。时钟处理看见 `current` 非空就会 `yield`。这次切换必须发生在任务自己的栈上，不能发生在调度器还没换栈的时候。

没有可运行任务时，`sti; hlt; cli` 先开中断再停机。时钟到来后，处理程序发现 `current` 为空，只做完 EOI 就 `iretq`。`hlt` 之后的 `cli` 再把中断关掉，然后回到挑选。

### `swtch`

```text
void swtch(uint64_t *save_rsp, uint64_t *load_rsp);
```

System V 下 `rdi` 是 `save_rsp`，`rsi` 是 `load_rsp`。函数先压入 `rbp、rbx、r12、r13、r14、r15`，把这时的 `rsp` 存进 `*save_rsp`，再把 `rsp` 换成 `*load_rsp`，按相反顺序弹出，然后 `ret`。

调用者保存的寄存器不在这里处理。从调度器调用时，活的数据都在局部变量和 `sched_rsp` 里。从时钟调用时，`irq_timer` 已经把调用者保存寄存器和它自己要用的 `rbx` 压在任务栈上，那些值留在 `yield` 的调用帧下面，等这个任务下次回来再弹出。

`yield_if_task` 在 `current` 为空时直接返回。否则把状态改回 `READY`，`swtch` 到 `sched_rsp`。调度器从自己的 `swtch` 返回后把 `current` 清零，继续挑下一个。被让出的任务下次被挑中时，从 `yield_if_task` 里的 `swtch` 返回，再回到 `irq_timer`，最后 `iretq`。

## 中断

### 门

长模式下每个 IDT 项 16 字节。`set_gate` 填写：

```text
偏移 0..15
代码段选择子 0x08
IST = 0
类型 0x8E        P=1，DPL=0，中断门
偏移 16..31
偏移 32..63
保留 0
```

中断门在进入时由 CPU 清掉 `RFLAGS.IF`。0 到 31 号门指向 `isr0` 到 `isr31`，32 号门指向 `irq_timer`。其余向量没有门。`lidt` 在 `sti` 之前执行。

### 异常桩

CPU 对 8、10、11、12、13、14、17、21 号异常会压入错误码，对应的桩只再压向量号。其余异常的桩先压一个 0，再压向量号。这样 `trap_common` 看到的栈总是：

```text
高地址
  向量号
  错误码或 0
rsp
```

`trap_common` 把向量放进 `rdi`、错误码放进 `rsi`，把栈按 16 字节对齐后调用 `trap_handler`。处理函数经串口打印 `trap`、向量和 `err`。向量 14 是页错误，再读 `CR2` 打印出错的线性地址。然后 `panic`，关中断并 `hlt`。这一版不从异常返回，也不做缺页换入。页错误表示内核写错了地址。

### PIC

上电时 8259 的 IRQ0 到 IRQ7 占用向量 8 到 15，和 CPU 异常重叠。`pic_init` 发四条初始化命令：

| 步骤 | 主片 `0x20` / `0x21` | 从片 `0xA0` / `0xA1` |
| --- | --- | --- |
| ICW1 | `0x11`，需要 ICW4 | `0x11` |
| ICW2 | 向量基址 32 | 向量基址 40 |
| ICW3 | `4`，从片接在 IRQ2 | `2`，级联号是 2 |
| ICW4 | `0x01`，8086 模式 | `0x01` |

每条命令之间对端口 `0x80` 写 0，给旧 PIC 一个总线周期的间隔。

然后写屏蔽字：主片 `0xFE`，只有 bit0 是 0，所以只放行 IRQ0；从片 `0xFF`，全部屏蔽。时钟中断因此固定从向量 32 进来。

处理程序在恢复被打断的代码之前，向主片命令口 `0x20` 写 `0x20`。这是 EOI。不写的话，这一路 IRQ 不会再次送来。

### PIT

`pit_init` 对命令口 `0x43` 写 `0x36`：通道 0，先低字节后高字节，模式 3 方波，二进制计数。除数 `11932` 分成两个字节写入数据口 `0x40`。PIT 的输入频率约 1193182Hz，除以 11932 约等于 100Hz。

### 时钟帧

CPU 进入 `irq_timer` 之前已经压好 `SS、RSP、RFLAGS、CS、RIP`。桩再压 `rax、rcx、rdx、rsi、rdi、r8、r9、r10、r11、rbx`，向 PIC 发 EOI，把对齐前的 `rsp` 留在 `rbx`，然后调用 `yield_if_task`。

`yield` 若换栈，这一整帧留在该任务的栈上。这个任务下次从 `swtch` 回来，`yield_if_task` 返回，桩把 `rsp` 从 `rbx` 设回去，弹出寄存器，`iretq`。`iretq` 恢复的 `RFLAGS` 里 IF 为 1，所以它会继续被下一次时钟打断。

`rbx` 在这里有两层用途：桩要用它记住对齐前的栈指针；`yield_if_task` 是 C 函数，可以改写 `rbx`，但 ABI 要求它返回前恢复。所以 `call` 之后 `rbx` 仍是那只旧栈指针。

调度器栈上如果正好发生时钟中断，`current` 为空，`yield_if_task` 立即返回，同一条路径 `iretq`。不会在调度器栈上再做一次 `swtch`。

## 串口

COM1 数据口是 `0x3f8`，线状态口是 `0x3fd`。`serial_putc` 等到状态位 5 变成 1，表示发送保持寄存器空，再把字节写到 `0x3f8`。`serial_hex` 固定打印 `0x` 和 16 个十六进制字符。`panic` 打印 `panic: ` 和消息，然后关中断、`hlt`。

`make run` 用 `-serial file:serial.log` 把这个端口接到文件。curses 占用当前终端来显示 VGA，串口字节在 `serial.log` 里。

## 编译选项

| 选项 | 原因 |
| --- | --- |
| `-ffreestanding` | 不链接宿主 C 库，不假定 `main` 是入口 |
| `-fno-pie` | 地址在链接时确定，不使用 GOT |
| `-fno-stack-protector` | 没有 `__stack_chk_fail` |
| `-mno-red-zone` | System V 允许使用 `RSP` 下 128 字节。中断帧正好压在当前栈顶，必须关掉 |
| `-mgeneral-regs-only` | 没有设置 `CR4.OSFXSR`。编译器若发出 SSE 指令，CPU 会产生 `#UD` |
| `-fno-asynchronous-unwind-tables` | 不生成 `.eh_frame`，内核不用它展开栈 |
| `-O1` | 生成可读的代码，同时仍会省略多余的栈访问 |
| `-z max-page-size=0x1000` | 把 Multiboot 头留在文件前 8192 字节 |
| `-z noexecstack` | 配合汇编里的 `.note.GNU-stack`，去掉链接器对可执行栈的提示 |

`boot.S`、`swtch.S`、`trap.S` 末尾都有空的 `.note.GNU-stack`。没有这段注记时，链接器会警告栈可执行。

装载段目前同时带读、写、执行，因为代码、数据和 `.bss` 在同一个 `PT_LOAD` 里。`grub-file` 和 QEMU 都接受这样的 Multiboot 映像。

## 和 Linux 6.10 的对应

模式切换只取这些位置：

| 这里 | Linux 6.10 |
| --- | --- |
| `_start` 已在 32 位、分页关闭 | `arch/x86/boot/compressed/head_64.S` 的 `startup_32` |
| `CR4.PAE` | 同文件约第 168 行 |
| 清页表并填写各级表项 | 约第 200 到 232 行 |
| `CR3` | 约第 235 行 |
| `MSR_EFER` 的 `LME` | `msr-index.h` 里 `MSR_EFER = 0xC0000080`，约第 239 到 242 行 |
| `CR0_STATE` 后远跳转 | `processor-flags.h` 约第 177 行；`head_64.S` 约第 273 到 281 行用 `lret` |
| `entry64` | 同文件另一个符号 `startup_64`，`.org 0x200` |

Linux 用 6 页映射 4GB，然后解压内核，再跳到解压后的另一个 `startup_64`。这里用 3 页映射 1GB，远跳转之后直接 `call kmain`。

内存、调度和中断各自只留一个动作：位图代替 `e820` 到伙伴系统，固定数组和递增下标代替 `pick_eevdf()`，一张 IDT 加 8259/PIT 代替 `init_IRQ()` 和时钟子系统。`fair_sched_class` 这个名字在 6.10 里还在，挑选函数已经是 `pick_eevdf()`。

## 这一版没有的东西

用户态、系统调用、多核、优先级、睡眠队列、缺页换入、内核堆、APIC 和 IOAPIC。`user/` 里的程序就是内核里的普通函数。需要小块内存时继续用静态数组。任务栈是唯一从 `pmm_alloc` 取走的页。异常只打印后停机。

`md/DESIGN.md` 里写过一种检查：把可用内存故意收成只剩一页时，第二个 `task_create` 会因为 `panic("no stack page")` 停住。主路径没有做这个实验。
