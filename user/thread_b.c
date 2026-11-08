#include "kernel.h"

/* 连续输出 B。按 q 回到菜单。 */
void thread_b_main(void)
{
	vga_puts("thread_b running, press q to return\n");
	serial_puts("thread_b running, press q to return\n");
	for (;;) {
		if (kbd_poll() == 'q')
			return;
		serial_putc('B');
		vga_putc('B');
	}
}
