#include "kernel.h"

/* 帮助页。按 q 回到菜单。 */
void help_main(void)
{
	vga_puts("help\n");
	serial_puts("help\n");
	vga_puts("1  show this page\n");
	serial_puts("1  show this page\n");
	vga_puts("2  run thread_a, print A\n");
	serial_puts("2  run thread_a, print A\n");
	vga_puts("3  run thread_b, print B\n");
	serial_puts("3  run thread_b, print B\n");
	vga_puts("4  power off\n");
	serial_puts("4  power off\n");
	vga_puts("q  return to menu\n");
	serial_puts("q  return to menu\n");
	for (;;) {
		if (kbd_poll() == 'q')
			return;
	}
}
