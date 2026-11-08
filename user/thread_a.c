#include "kapi.h"

/* 连续输出 A。按 q 回到菜单。 */
void main(struct kapi *api)
{
	api->puts("thread_a running, press q to return\n");
	for (;;) {
		if (api->poll() == 'q')
			return;
		api->serial_putc('A');
		api->putc('A');
	}
}
