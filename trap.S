/*
 * CPU 异常统一从 trap_common 进入。带错误码的向量不能再压一个 0。
 * 时钟 IRQ0 单独处理：发 EOI，必要时让出 CPU，再 iretq。
 */
	.code64

	.macro isr_noerr num
	.global isr\num
isr\num:
	pushq $0
	pushq $\num
	jmp trap_common
	.endm

	.macro isr_err num
	.global isr\num
isr\num:
	pushq $\num
	jmp trap_common
	.endm

	isr_noerr 0
	isr_noerr 1
	isr_noerr 2
	isr_noerr 3
	isr_noerr 4
	isr_noerr 5
	isr_noerr 6
	isr_noerr 7
	isr_err   8
	isr_noerr 9
	isr_err   10
	isr_err   11
	isr_err   12
	isr_err   13
	isr_err   14
	isr_noerr 15
	isr_noerr 16
	isr_err   17
	isr_noerr 18
	isr_noerr 19
	isr_noerr 20
	isr_err   21
	isr_noerr 22
	isr_noerr 23
	isr_noerr 24
	isr_noerr 25
	isr_noerr 26
	isr_noerr 27
	isr_noerr 28
	isr_noerr 29
	isr_noerr 30
	isr_noerr 31

	.section .rodata
	.global isr_table
isr_table:
	.quad isr0
	.quad isr1
	.quad isr2
	.quad isr3
	.quad isr4
	.quad isr5
	.quad isr6
	.quad isr7
	.quad isr8
	.quad isr9
	.quad isr10
	.quad isr11
	.quad isr12
	.quad isr13
	.quad isr14
	.quad isr15
	.quad isr16
	.quad isr17
	.quad isr18
	.quad isr19
	.quad isr20
	.quad isr21
	.quad isr22
	.quad isr23
	.quad isr24
	.quad isr25
	.quad isr26
	.quad isr27
	.quad isr28
	.quad isr29
	.quad isr30
	.quad isr31

	.text
trap_common:
	cld
	movq (%rsp), %rdi
	movq 8(%rsp), %rsi
	andq $-16, %rsp
	call trap_handler
1:
	hlt
	jmp 1b

	.global irq_timer
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

	movb $0x20, %al
	outb %al, $0x20

	movq %rsp, %rbx
	andq $-16, %rsp
	call yield_if_task
	movq %rbx, %rsp

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

	.section .note.GNU-stack, "", @progbits
