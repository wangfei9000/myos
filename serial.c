#include "kernel.h"

static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

void serial_putc(char c)
{
	while ((inb(0x3fd) & 0x20) == 0)
		;
	outb(0x3f8, (uint8_t)c);
}

void serial_puts(const char *s)
{
	while (*s)
		serial_putc(*s++);
}

void serial_hex(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";
	serial_puts("0x");
	for (int shift = 60; shift >= 0; shift -= 4)
		serial_putc(digits[(value >> shift) & 0xf]);
}

void panic(const char *msg)
{
	serial_puts("panic: ");
	serial_puts(msg);
	serial_putc('\n');
	for (;;)
		__asm__ volatile("cli; hlt");
}
