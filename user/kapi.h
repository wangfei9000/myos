#pragma once

#include <stdint.h>

/* 用户程序能调用的内核服务。两边都必须按这个顺序编译。 */
struct kapi {
	uint32_t version;
	void (*putc)(char c);
	void (*puts)(const char *s);
	void (*serial_putc)(char c);
	int (*poll)(void);
};
