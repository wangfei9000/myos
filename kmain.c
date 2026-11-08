#include "kernel.h"

/* 画出主菜单。线程运行时按 q 会回到这里。 */
static void show_menu(void)
{
	vga_clear();
	vga_puts("menu\n");
	vga_puts("1  help\n");
	vga_puts("2  run thread_a\n");
	vga_puts("3  run thread_b\n");
	vga_puts("4  power off\n");
	vga_puts("press q while a thread runs to return\n");
	serial_puts("\nmenu: 1 help, 2 thread_a, 3 thread_b, 4 power off, q back\n");
}

/* 帮助页。按 q 回到主菜单，按 2 或 3 直接运行对应线程。 */
static void show_help(void)
{
	vga_clear();
	vga_puts("help\n");
	vga_puts("1  show this page\n");
	vga_puts("2  run thread_a, print A\n");
	vga_puts("3  run thread_b, print B\n");
	vga_puts("4  power off\n");
	vga_puts("q  return to menu\n");
	serial_puts("help: 1 help, 2 A, 3 B, 4 off, q menu\n");
}

/* 等到菜单键。时钟中断会把 hlt 唤醒，再继续查键盘。 */
static int wait_key(void)
{
	int c;

	while ((c = kbd_poll()) < 0)
		__asm__ volatile("hlt");
	return c;
}

/* 连续输出 A。按下 q 后返回，由菜单重新显示主菜单。 */
static void thread_a(void)
{
	vga_clear();
	vga_status("thread_a running, press q to return");
	serial_puts("thread_a start\n");
	for (;;) {
		if (kbd_poll() == 'q')
			break;
		serial_putc('A');
		vga_putc('A');
	}
	serial_puts("\nthread_a stop\n");
}

/* 连续输出 B。按下 q 后返回主菜单。 */
static void thread_b(void)
{
	vga_clear();
	vga_status("thread_b running, press q to return");
	serial_puts("thread_b start\n");
	for (;;) {
		if (kbd_poll() == 'q')
			break;
		serial_putc('B');
		vga_putc('B');
	}
	serial_puts("\nthread_b stop\n");
}

/* 向端口写一个字。QEMU 用 0x604 接收关机命令。 */
static inline void outw(uint16_t port, uint16_t value)
{
	__asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

/* 退出内核。先写 QEMU 关机端口，写不走就停机。 */
static void poweroff(void)
{
	vga_clear();
	vga_puts("power off\n");
	serial_puts("power off\n");
	outw(0x604, 0x2000);
	outw(0xb004, 0x2000);
	for (;;)
		__asm__ volatile("cli; hlt");
}

/* 主菜单循环。1 帮助，2 运行 A，3 运行 B，4 关机，q 回到主菜单。 */
static void menu(void)
{
	show_menu();
	for (;;) {
		switch (wait_key()) {
		case '1':
			show_help();
			break;
		case '2':
			thread_a();
			show_menu();
			break;
		case '3':
			thread_b();
			show_menu();
			break;
		case '4':
			poweroff();
			break;
		case 'q':
			show_menu();
			break;
		}
	}
}

/*
 * entry64 调用的 C 入口。初始化完成后开中断，进入菜单。
 * 菜单直接调用线程函数，按 q 后函数返回，所以不再进入 scheduler。
 */
void kmain(void)
{
	pmm_init();
	task_init();
	idt_init();
	pic_init();
	pit_init();
	__asm__ volatile("sti");
	menu();
}
