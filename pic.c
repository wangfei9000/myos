#include "kernel.h"

/* 向 I/O 端口写一个字节。 */
static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* 写一个不用的端口，给旧 8259 一个总线周期的间隔。 */
static void io_wait(void)
{
	outb(0x80, 0);
}

/*
 * 把 8259 的向量从 CPU 异常区挪开。主片基址 32，从片基址 40。
 * 主片只放行 IRQ0，从片全部屏蔽，所以只有时钟能进来。
 */
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

/*
 * 设置 PIT 通道 0 为模式 3 方波。除数 11932，输入约 1193182Hz，
 * 得到约 100Hz 的 IRQ0。
 */
void pit_init(void)
{
	uint16_t divisor = 11932;
	outb(0x43, 0x36);
	outb(0x40, (uint8_t)divisor);
	outb(0x40, (uint8_t)(divisor >> 8));
}
