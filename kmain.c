#include "kernel.h"

/* 内核线程。一直写串口，自己不调用 yield，靠时钟中断被换下。 */
static void thread_a(void)
{
	for (;;)
		serial_putc('A');
}

/* 和 thread_a 相同，输出 B，用来观察轮转。 */
static void thread_b(void)
{
	for (;;)
		serial_putc('B');
}

/*
 * entry64 调用的 C 入口。中断保持关闭，直到两个任务都创建完。
 * sti 之后进入 scheduler，不再返回。
 */
void kmain(void)
{
	pmm_init();
	task_init();
	idt_init();
	pic_init();
	pit_init();
	task_create(thread_a);
	task_create(thread_b);
	__asm__ volatile("sti");
	scheduler();
}
