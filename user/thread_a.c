#include "kernel.h"

/* 连续输出 A。按 q 回到菜单。 */
void thread_a_main(void)
{
	vga_puts("thread_a running, press q to return\n");
	serial_puts("thread_a running, press q to return\n");
	for (;;) {
		if (kbd_poll() == 'q')
			return;
		serial_putc('A');
		vga_putc('A');
	}
}
