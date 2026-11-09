# trap.S

`trap.S` 在 64 位长模式里执行。它给异常 0 到 31 各做一个短桩，再单独处理时钟中断。`idt.c` 把 `isr_table` 里的地址填进前 32 个门，把向量 32 指到 `irq_timer`。

CPU 进入中断门时已经清掉 `RFLAGS.IF`。异常桩最后停机。时钟桩发完 EOI，必要时让出 CPU，再用 `iretq` 回到被打断的地方。

## 两条桩

CPU 对向量 8、10、11、12、13、14、17、21 会自己压入错误码。其余异常不压错误码。两条宏把栈整理成同一种形状：先是错误码或 0，上面再压向量号。

```text
	.macro isr_noerr num
	.global isr\num
isr\num:
	pushq $0
	pushq $\num
	jmp trap_common
	.endm
```

没有错误码的向量用这一条。`pushq $0` 补一个假的错误码。`pushq $\num` 压入向量号。然后跳到 `trap_common`。`\num` 是调用宏时传入的数字，所以 `isr_noerr 0` 展开成符号 `isr0`。

```text
	.macro isr_err num
	.global isr\num
isr\num:
	pushq $\num
	jmp trap_common
	.endm
```

CPU 已经压过错误码，这里只再压向量号，避免多出一个 0 把栈错开。

后面 32 行就是在用这两条宏。`isr_err` 只用在 8、10、11、12、13、14、17、21。其余编号用 `isr_noerr`。展开之后，每个 `isrN` 都是全局符号，最后都进 `trap_common`。

## 函数指针表

```text
isr_table:
	.quad isr0
	.quad isr1
	...
	.quad isr31
```

32 个 8 字节地址，下标就是向量号。`idt.c` 的 `set_gate(i, isr_table[i])` 用这张表填 IDT。表放在 `.rodata`，运行时只读。

## 异常公共入口

进入 `trap_common` 时，栈顶 16 字节是：

```text
高地址
  错误码或 0
  向量号
rsp
```

```text
trap_common:
	cld
	movq (%rsp), %rdi
	movq 8(%rsp), %rsi
	andq $-16, %rsp
	call trap_handler
1:
	hlt
	jmp 1b
```

`cld` 清掉方向标志。`(%rsp)` 是刚才压入的向量号，放进 `RDI`，这是 `trap_handler` 的第一个参数。`8(%rsp)` 是错误码或 0，放进 `RSI`，这是第二个参数。

`andq $-16, %rsp` 把栈指针按 16 字节对齐，满足 System V 调用约定。这一步会丢掉刚才压的两个值，也会丢掉返回用的中断帧。`trap_handler` 打印之后停机，所以不再 `iretq`。

`call trap_handler` 进入 C。万一它返回了，`hlt` 和 `jmp 1b` 停在原地。

## 时钟中断

向量 32 是 IRQ0。CPU 进入 `irq_timer` 之前已经压好 `SS`、`RSP`、`RFLAGS`、`CS`、`RIP`。

```text
irq_timer:
	cld
	pushq %rax
	pushq %rcx
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %rbx
```

`cld` 清方向标志。接着压 10 个寄存器。`RAX`、`RCX`、`RDX`、`RSI`、`RDI`、`R8` 到 `R11` 是 C 函数可以随意改写的寄存器。`RBX` 本来由被调用者保存，这里要拿它记栈指针，所以也压一份。

```text
	movb $0x20, %al
	outb %al, $0x20
```

向主 PIC 的命令口 `0x20` 写 `0x20`。这是 EOI，告诉 8259 这一次 IRQ0 已经处理。不写的话，时钟不会再次送来。

```text
	movq %rsp, %rbx
	andq $-16, %rsp
	call yield_if_task
	movq %rbx, %rsp
```

`RBX` 记住对齐前的栈指针。`andq $-16, %rsp` 把栈按 16 字节对齐，然后调用 `yield_if_task`。

菜单和 `user/` 里的函数运行时没有当前任务，`yield_if_task` 直接返回。如果当时确实在某个任务栈上，它会换成调度器栈；这个任务以后被挑回来时，从 `call` 的下一条继续。`RBX` 由被调用者保存，所以回来时仍是对齐前的栈指针。`movq %rbx, %rsp` 把栈恢复回去。

```text
	popq %rbx
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rax
	iretq
```

按相反顺序弹出那 10 个寄存器。`iretq` 再弹出 `RIP`、`CS`、`RFLAGS`、`RSP`、`SS`，回到被时钟打断的指令。恢复出来的 `RFLAGS` 里 IF 为 1，所以后面还能再进时钟中断。

文件最后的 `.section .note.GNU-stack` 是空的链接器注记，用来标记栈不可执行。CPU 不执行它。
