#include "kernel.h"

struct item {
	char key;
	const char *title;
	void (*fn)(void);
};

static const struct item items[] = {
	{ '1', "help", help_main },
	{ '2', "run thread_a", thread_a_main },
	{ '3', "run thread_b", thread_b_main },
};

/* 等到一个键。时钟中断会把 hlt 唤醒，再继续查键盘。 */
static int wait_key(void)
{
	int c;

	while ((c = kbd_poll()) < 0)
		__asm__ volatile("hlt");
	return c;
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

/* 按 items 逐行显示。关机项固定留在内核里。 */
static void show_menu(void)
{
	vga_clear();
	vga_puts("menu\n");
	serial_puts("\nmenu\n");
	for (unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
		vga_putc(items[i].key);
		vga_puts("  ");
		vga_puts(items[i].title);
		vga_putc('\n');
		serial_putc(items[i].key);
		serial_puts("  ");
		serial_puts(items[i].title);
		serial_putc('\n');
	}
	vga_puts("4  power off\n");
	serial_puts("4  power off\n");
}

/* 按键等于表里的 key 时调用对应函数。q 只在程序内部返回。 */
static void menu(void)
{
	for (;;) {
		show_menu();
		int key = wait_key();
		if (key == '4')
			poweroff();
		for (unsigned i = 0; i < sizeof(items) / sizeof(items[0]); i++) {
			if (items[i].key == key) {
				items[i].fn();
				break;
			}
		}
	}
}

/*
 * entry64 调用的 C 入口。初始化完成后开中断，进入菜单。
 * 可运行的程序在 user/ 里，由上面的 items 引用。
 */
void kmain(void)
{
	pmm_init();
	task_init();
	idt_init();
	pic_init();
	pit_init();
	/* 页表、IDT、PIC 和 PIT 都已就绪。sti 置位 IF，时钟中断从此可以进来。 */
	__asm__ volatile("sti");
	menu();
}
