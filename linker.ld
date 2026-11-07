OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
	/* GRUB / QEMU 把这段映像装到 1MB。入口代码仍是 32 位。 */
	. = 1M;

	.text : {
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
	}

	.data : {
		*(.data)
		*(.data.*)
	}

	.bss : {
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4096);
		pml4 = .;
		. += 4096;
		pdpt = .;
		. += 4096;
		pd = .;
		. += 4096;
		. = ALIGN(16);
		stack_bottom = .;
		. += 4096;
		stack_top = .;
	}

	/DISCARD/ : {
		*(.note.*)
		*(.comment)
		*(.eh_frame)
		*(.eh_frame_hdr)
	}
}
