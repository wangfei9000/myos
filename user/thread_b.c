#include "kapi.h"

/* 连续输出 B。按 q 回到菜单。 */
void main(struct kapi *api)
{
	api->puts("thread_b running, press q to return\n");
	for (;;) {
		if (api->poll() == 'q')
			return;
		api->serial_putc('B');
		api->putc('B');
	}
}
