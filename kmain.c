#include "kernel.h"

static void thread_a(void)
{
	for (;;)
		serial_putc('A');
}

static void thread_b(void)
{
	for (;;)
		serial_putc('B');
}

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
