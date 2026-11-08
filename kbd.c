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
 * 读一个按下的键。没有新键时返回 -1。
 * 0x64 第 0 位为 1 表示 0x60 里有扫描码。只认按下，不认松开。
 * IRQ1 仍被屏蔽，菜单直接查询键盘控制器。
 */
int kbd_poll(void)
{
	uint8_t sc;

	if ((inb(0x64) & 0x01) == 0)
		return -1;
	sc = inb(0x60);
	if (sc & 0x80)
		return -1;
	switch (sc) {
	case 0x02:
		return '1';
	case 0x03:
		return '2';
	case 0x04:
		return '3';
	case 0x05:
		return '4';
	case 0x10:
		return 'q';
	default:
		return -1;
	}
}
