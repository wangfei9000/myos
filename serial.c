#include "kernel.h"

/* 向 I/O 端口写一个字节。 */
static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* 从 I/O 端口读一个字节。 */
static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

/*
 * 向 COM1 写一个字符。0x3fd 的第 5 位为 1 时发送寄存器空闲，
 * make run 带了 -serial stdio，QEMU 把这块虚拟 COM1 接到启动它的终端。所以写进 0x3f8 的 A、B 会直接出现在屏幕上。
 */
void serial_putc(char c)
{
	while ((inb(0x3fd) & 0x20) == 0)
		;
	outb(0x3f8, (uint8_t)c);
}

/* 逐字节写出字符串，不额外加换行。 */
void serial_puts(const char *s)
{
	while (*s)
		serial_putc(*s++);
}

/* 固定输出 0x 和 16 个十六进制字符，高位在前。 */
void serial_hex(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	serial_puts("0x");
	for (int shift = 60; shift >= 0; shift -= 4)
		serial_putc(digits[(value >> shift) & 0xf]);
}

/* 串口打印 panic 消息，然后关中断并停机。 */
void panic(const char *msg)
{
	serial_puts("panic: ");
	serial_puts(msg);
	serial_putc('\n');
	for (;;)
		__asm__ volatile("cli; hlt");
}
