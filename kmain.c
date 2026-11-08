#include "kernel.h"

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

/* 菜单行来自 MYFS 目录。关机项固定留在内核里。 */
static void show_menu(void)
{
	int order[64];
	int n = fs_count();

	vga_clear();
	vga_puts("menu\n");
	serial_puts("\nmenu\n");
	for (int i = 0; i < n; i++)
		order[i] = i;
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			if (fs_key(order[j]) < fs_key(order[i])) {
				int tmp = order[i];
				order[i] = order[j];
				order[j] = tmp;
			}
		}
	}
	for (int i = 0; i < n; i++) {
		char key = fs_key(order[i]);
		const char *title = fs_title(order[i]);
		vga_putc(key);
		vga_puts("  ");
		vga_puts(title);
		vga_putc('\n');
		serial_putc(key);
		serial_puts("  ");
		serial_puts(title);
		serial_putc('\n');
	}
	vga_puts("4  power off\n");
	serial_puts("4  power off\n");
}

/* 按目录里的键运行对应程序。q 只在程序内部返回。 */
static void menu(void)
{
	fs_init();
	for (;;) {
		show_menu();
		int key = wait_key();
		if (key == '4')
			poweroff();
		for (int i = 0; i < fs_count(); i++) {
			if (fs_key(i) == key) {
				fs_run(i);
				break;
			}
		}
	}
}

/*
 * entry64 调用的 C 入口。初始化完成后开中断，进入菜单。
 * 可运行的程序在磁盘上，不在这个文件里。
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
