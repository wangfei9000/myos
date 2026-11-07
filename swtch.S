/*
 * void swtch(uint64_t *save_rsp, uint64_t *load_rsp);
 *
 * 栈上从低地址到高地址依次是 r15、r14、r13、r12、rbx、rbp、返回地址。
 * 返回地址对新任务来说是 task_entry。
 */
	.code64
	.global swtch
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

	.global task_entry
task_entry:
	sti
	popq %rax
	call *%rax
1:
	hlt
	jmp 1b

	.section .note.GNU-stack, "", @progbits
