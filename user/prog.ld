OUTPUT_FORMAT(elf64-x86-64)
ENTRY(main)

SECTIONS
{
	. = 0x1000000;
	.proghdr : { *(.proghdr) }
	. = 0x1000010;
	.text : {
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
		*(.data)
		*(.data.*)
	}
	.bss : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		_end = .;
	}
	/DISCARD/ : {
		*(.note.*)
		*(.comment)
		*(.eh_frame)
		*(.eh_frame_hdr)
	}
}
