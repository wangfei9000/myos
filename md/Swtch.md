# swtch.S

`swtch.S` 在 64 位长模式里执行。`swtch` 只交换栈。栈上留着被调用者必须保存的 6 个寄存器和返回地址。新任务的返回地址是 `task_entry`，由 `task.c` 在创建任务时写好。

调用约定是：

```text
void swtch(uint64_t *save_rsp, uint64_t *load_rsp);
```

`RDI` 是 `save_rsp`，`RSI` 是 `load_rsp`。

## 换栈

```text
swtch:
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	movq %rsp, (%rdi)
	movq (%rsi), %rsp
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp
	ret
```

先把 `RBP`、`RBX`、`R12`、`R13`、`R14`、`R15` 压进当前栈。这 6 个是 System V 要求被调用者保存的寄存器。调用者保存的寄存器不在这里处理，它们留在调用 `swtch` 的那一帧外面。

压完之后，`movq %rsp, (%rdi)` 把当前 `RSP` 存进 `*save_rsp`。调度器调用时，这个地址是 `sched_rsp`。任务让出时，这个地址是该任务自己的 `rsp`。

`movq (%rsi), %rsp` 把 `RSP` 换成 `*load_rsp`。从这一条开始，后面的 `popq` 和 `ret` 用的是另一条栈。

6 个 `popq` 按相反顺序弹出寄存器。`ret` 弹出返回地址并跳过去。切回一个老任务时，返回地址是它上次调用 `swtch` 的下一条指令。第一次进入新任务时，`task_create` 把返回地址写成了 `task_entry`，所以 `ret` 进 `task_entry`。

`task_create` 从栈的高地址往低地址写了 8 个 8 字节槽：

```text
高地址
  fn
  task_entry
  rbp
  rbx
  r12
  r13
  r14
  r15
rsp 在这里
```

`swtch` 弹出下面 6 个寄存器，再 `ret` 到 `task_entry`。`fn` 还留在栈上，等 `task_entry` 来取。

## 新任务的第一条指令

```text
task_entry:
	sti
	popq %rax
	call *%rax
1:
	hlt
	jmp 1b
```

`swtch` 不保存 `RFLAGS`。调度器在切出前执行了 `cli`，新任务的栈上又没有可以 `iretq` 的中断帧，所以这里用 `sti` 自己打开中断。

`popq %rax` 弹出 `task_create` 留下的函数指针。`call *%rax` 调用它。

函数如果返回，`hlt` 停机，`jmp 1b` 跳回标号 `1`，一直停着。菜单里的 `help_main`、`thread_a_main`、`thread_b_main` 不走这里，它们由 `kmain` 直接调用。

文件最后的 `.section .note.GNU-stack` 是空的链接器注记，用来标记栈不可执行。CPU 不执行它。
