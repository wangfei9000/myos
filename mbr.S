/*
 * 磁盘第一个扇区。BIOS 把它载入 0x7C00，此时是 16 位实模式。
 * 从 LBA 1 读出内核，拷到 0x100000，打开保护模式后跳到 _start。
 */
	.code16
	.global _start
_start:
	cli
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw $0x7c00, %sp
	movb %dl, drive
	lgdt gdt_desc

	inb $0x92, %al
	orb $2, %al
	outb %al, $0x92

	movb $0x41, %ah
	movw $0x55aa, %bx
	movb drive, %dl
	int $0x13
	jc fail
	cmpw $0xaa55, %bx
	jne fail

	movl $1, lba
	movl $0x100000, dest
	movw $KERNEL_SECTORS, left

load_loop:
	cmpw $0, left
	je enter_pm
	movw left, %ax
	cmpw $32, %ax
	jbe have_count
	movw $32, %ax
have_count:
	movw %ax, chunk
	movw %ax, dap_count
	movw $0x8000, dap_off
	movw $0, dap_seg
	movl lba, %eax
	movl %eax, dap_lba
	movl $0, dap_lba_hi
	movb $0x42, %ah
	movw $dap, %si
	movb drive, %dl
	int $0x13
	jc fail
	call copy_chunk
	movzwl chunk, %eax
	addl %eax, lba
	shll $9, %eax
	addl %eax, dest
	movw chunk, %ax
	subw %ax, left
	jmp load_loop

copy_chunk:
	cli
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0
	ljmp $0x08, $copy32

	.code32
copy32:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movl $0x8000, %esi
	movl dest, %edi
	movzwl chunk, %ecx
	shll $7, %ecx
	cld
	rep movsl
	ljmp $0x18, $copy16

	.code16
copy16:
	movl %cr0, %eax
	andb $0xfe, %al
	movl %eax, %cr0
	ljmp $0x0000, $copy_real
copy_real:
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	ret

enter_pm:
	cli
	movl %cr0, %eax
	orl $1, %eax
	movl %eax, %cr0
	ljmp $0x08, $pm_entry

	.code32
pm_entry:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movl $0x7c00, %esp
	movl $KERNEL_ENTRY, %eax
	jmp *%eax

	.code16
fail:
	movb $0x0e, %ah
	movb $'E', %al
	int $0x10
	hlt
	jmp fail

drive:
	.byte 0
lba:
	.long 1
dest:
	.long 0x100000
chunk:
	.word 0
left:
	.word 0
dap:
	.byte 0x10, 0
dap_count:
	.word 0
dap_off:
	.word 0x8000
dap_seg:
	.word 0
dap_lba:
	.long 0
dap_lba_hi:
	.long 0

	.align 8
gdt:
	.quad 0
	.quad 0x00cf9a000000ffff
	.quad 0x00cf92000000ffff
	.quad 0x00009a000000ffff
gdt_end:

gdt_desc:
	.word gdt_end - gdt - 1
	.long gdt

	.org 510
	.word 0xaa55
