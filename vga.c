#include "kernel.h"

/*
 * 80x25 文字模式。左上角在物理地址 0xB8000，恒等映射后可以直接写。
 * 每个格子 2 字节：低字节是字符，高字节是颜色。0x07 是黑底灰字。
 */
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_COLOR 0x07

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static int row;
static int col;

/* 向端口写一个字节，用来移动硬件光标。 */
static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* 把硬件光标移到当前行列。 */
static void vga_cursor(void)
{
	uint16_t pos = (uint16_t)(row * VGA_COLS + col);
	outb(0x3d4, 0x0f);
	outb(0x3d5, (uint8_t)pos);
	outb(0x3d4, 0x0e);
	outb(0x3d5, (uint8_t)(pos >> 8));
}

/* 满屏后整屏上移一行，最后一行清空。 */
static void vga_scroll(void)
{
	for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
		vga[i] = vga[i + VGA_COLS];
	for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
		vga[i] = (uint16_t)' ' | (VGA_COLOR << 8);
	row = VGA_ROWS - 1;
	col = 0;
}

/* 向屏幕写一个字符。换行或写满一行后进入下一行。 */
void vga_putc(char c)
{
	if (c == '\n') {
		col = 0;
		if (++row == VGA_ROWS)
			vga_scroll();
		vga_cursor();
		return;
	}
	vga[row * VGA_COLS + col] = (uint16_t)(uint8_t)c | (VGA_COLOR << 8);
	if (++col == VGA_COLS) {
		col = 0;
		if (++row == VGA_ROWS)
			vga_scroll();
	}
	vga_cursor();
}

/* 把字符串逐字符写到屏幕，不额外加换行。 */
void vga_puts(const char *s)
{
	while (*s)
		vga_putc(*s++);
}
