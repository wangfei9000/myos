#include "kernel.h"

static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
	outb(0x80, 0);
}

void pic_init(void)
{
	outb(0x20, 0x11);
	io_wait();
	outb(0xa0, 0x11);
	io_wait();
	outb(0x21, 32);
	io_wait();
	outb(0xa1, 40);
	io_wait();
	outb(0x21, 4);
	io_wait();
	outb(0xa1, 2);
	io_wait();
	outb(0x21, 0x01);
	io_wait();
	outb(0xa1, 0x01);
	io_wait();

	/* 主片只留 IRQ0，从片全部屏蔽。 */
	outb(0x21, 0xfe);
	outb(0xa1, 0xff);
}

void pit_init(void)
{
	uint16_t divisor = 11932;
	outb(0x43, 0x36);
	outb(0x40, (uint8_t)divisor);
	outb(0x40, (uint8_t)(divisor >> 8));
}
