/*
 * 32 位保护模式跳到 64 位长模式。
 *
 * 入口已经是 32 位平坦保护模式，分页关闭。GRUB 按 Multiboot 把 CPU
 * 交到这里。这正是 Linux 压缩内核 startup_32 开始时的状态。
 *
 * 对照的 Linux 6.10 源码，只取模式切换本身：
 *   arch/x86/boot/compressed/head_64.S
 *     startup_32  第 168 行  打开 CR4.PAE
 *     第 200 行              清页表
 *     第 206 到 232 行       填 4 级 / 3 级 / 2 级页表
 *     第 235 行              把页表地址写入 CR3
 *     第 239 到 242 行       在 EFER 里打开 LME
 *     第 273 到 281 行       写 CR0 后 lret，进入 startup_64
 *   arch/x86/include/uapi/asm/processor-flags.h
 *     CR0_STATE  第 177 行
 *   arch/x86/include/asm/msr-index.h
 *     MSR_EFER = 0xc0000080，LME 是第 8 位
 *
 * 故意没做的事：实模式、A20、解压内核、KASLR、5 级页表、SEV。
 * Linux 用 6 页页表恒等映射 4GB。这里用 3 页映射低 1GB，足够跑这段代码。
 */

	.code32
	.section .text

	.set MB_MAGIC, 0x1BADB002
	.set CR0_PE, 1
	.set CR0_MP, 1 << 1
	.set CR0_ET, 1 << 4
	.set CR0_NE, 1 << 5
	.set CR0_WP, 1 << 16
	.set CR0_AM, 1 << 18
	.set CR0_PG, 1 << 31
	.set CR0_STATE, CR0_PE | CR0_MP | CR0_ET | CR0_NE | CR0_WP | CR0_AM | CR0_PG
	.set CR4_PAE, 1 << 5
	.set MSR_EFER, 0xC0000080
	.set EFER_LME, 1 << 8
	.set PTE_PRESENT, 0x1
	.set PTE_WRITE, 0x2
	.set PTE_PS, 0x80
	.set SEL_CODE64, 0x08
	.set SEL_DATA, 0x10

	.align 4
multiboot_header:
	.long MB_MAGIC
	.long 0
	.long -(MB_MAGIC)

	.global _start
_start:
	cli
	cld

	/*
	 * 先保住 EAX / EBX。磁盘启动没有 Multiboot 魔数，pmm 会退回 128MB。
	 * .bss 不能靠引导程序清零，这里自己从 __bss_start 清到 _end。
	 */
	movl %eax, %esi
	movl %ebx, %ebp
	movl $__bss_start, %edi
	movl $_end, %ecx
	subl %edi, %ecx
	xorl %eax, %eax
	rep stosb
	movl %esi, multiboot_magic
	movl %ebp, multiboot_info_phys

	/* 先换上我们自己的 GDT。64 位代码段的 L 位必须是 1，D 位必须是 0。 */
	lgdt gdt_ptr
	movw $SEL_DATA, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $stack_top, %esp

	/* CPUID 扩展叶 0x80000001 的 EDX 第 29 位表示支持长模式。 */
	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000001, %eax
	jb no_long_mode
	movl $0x80000001, %eax
	cpuid
	btl $29, %edx
	jnc no_long_mode

	/*
	 * 三张页表，每张 4KB，链接脚本把它们按 4KB 对齐。
	 * PML4[0] -> PDPT，PDPT[0] -> PD，PD 里 512 个 2MB 页。
	 * 覆盖物理地址 0 到 1GB，虚拟地址等于物理地址。
	 * Linux 对应项的低 32 位是 0x1007 和 0x183，多了 User 和 Accessed。
	 * 切换本身只需要 Present 和 Writable，2MB 页再加 PS。
	 */
	movl $pml4, %edi
	xorl %eax, %eax
	movl $(3 * 4096 / 4), %ecx
	rep stosl

	movl $pml4, %edi
	movl $pdpt, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, (%edi)

	movl $pdpt, %edi
	movl $pd, %eax
	orl $(PTE_PRESENT | PTE_WRITE), %eax
	movl %eax, (%edi)

	movl $pd, %edi
	movl $(PTE_PRESENT | PTE_WRITE | PTE_PS), %eax
	movl $512, %ecx
fill_pd:
	movl %eax, (%edi)
	addl $0x200000, %eax
	addl $8, %edi
	decl %ecx
	jnz fill_pd

	/* 下面四步的顺序不能换。先开 PAE，再装 CR3，再开 LME，最后开分页。 */
	movl %cr4, %eax
	orl $CR4_PAE, %eax
	movl %eax, %cr4

	movl $pml4, %eax
	movl %eax, %cr3

	movl $MSR_EFER, %ecx
	rdmsr
	orl $EFER_LME, %eax
	wrmsr

	/*
	 * 写完 CR0.PG 时 CPU 进入长模式，但 CS 仍是 32 位兼容段。
	 * 必须立刻用 L=1 的代码段做一次远跳转，之后的指令才按 64 位译码。
	 * Linux 在 head_64.S 第 273 到 281 行用 push + lret 做这件事。
	 */
	movl $CR0_STATE, %eax
	movl %eax, %cr0
	ljmpl $SEL_CODE64, $entry64

no_long_mode:
	movw $0x4f4e, 0xb8000
	movw $0x4f4f, 0xb8002
	hlt
	jmp no_long_mode

	.code64
entry64:
	/* 数据段清成空选择子。64 位寻址不再使用 DS 的基址。 */
	xorl %eax, %eax
	movl %eax, %ds
	movl %eax, %es
	movl %eax, %ss
	movl $stack_top, %esp
	call kmain
1:
	hlt
	jmp 1b

	.section .data
	.global multiboot_magic
	.global multiboot_info_phys
multiboot_magic:
	.long 0
multiboot_info_phys:
	.long 0

	.section .rodata
	.align 8
gdt:
	.quad 0
	/* 选择子 0x08：64 位代码。access=0x9a，flags=0xa，其中 L=1、D=0。 */
	.quad 0x00af9a000000ffff
	/* 选择子 0x10：数据段。远跳转前用来得到平坦的 DS/SS。 */
	.quad 0x00cf92000000ffff
gdt_end:

gdt_ptr:
	.word gdt_end - gdt - 1
	.long gdt

	.section .note.GNU-stack, "", @progbits
