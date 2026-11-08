OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS
{
	/* 运行地址是 0x7C00。LMA 从 0 开始，生成的扇区文件才不会前面填一截 0。 */
	. = 0x7C00;
	.text : AT(0) {
		*(.text)
	}
}
