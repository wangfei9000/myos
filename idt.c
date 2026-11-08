#include "kernel.h"

struct idt_entry {
	uint16_t off0;
	uint16_t sel;
	uint8_t ist;
	uint8_t type;
	uint16_t off1;
	uint32_t off2;
	uint32_t zero;
} __attribute__((packed));

static struct idt_entry idt[256];

extern uint64_t isr_table[];
extern void irq_timer(void);

/*
 * 填写一个 16 字节的中断门。选择子 0x08，类型 0x8E。
 * CPU 进入这种门时会清掉中断标志。
 */
static void set_gate(int vector, void *handler)
{
	uint64_t addr = (uint64_t)handler;
	idt[vector].off0 = (uint16_t)addr;
	idt[vector].sel = 0x08;
	idt[vector].ist = 0;
	idt[vector].type = 0x8e;
	idt[vector].off1 = (uint16_t)(addr >> 16);
	idt[vector].off2 = (uint32_t)(addr >> 32);
	idt[vector].zero = 0;
}

/* 异常 0 到 31 指向 isr_table，向量 32 指向时钟 irq_timer，然后 lidt。 */
void idt_init(void)
{
	for (int i = 0; i < 32; i++)
		set_gate(i, (void *)isr_table[i]);
	set_gate(32, irq_timer);

	struct {
		uint16_t limit;
		uint64_t base;
	} __attribute__((packed)) desc = {
		(uint16_t)(sizeof idt - 1),
		(uint64_t)idt
	};
	__asm__ volatile("lidt %0" : : "m"(desc));
}

/*
 * trap_common 调用。串口打印向量号和错误码。
 * 向量 14 是页错误，再打印 CR2。打印完停机，不从异常返回。
 */
void trap_handler(uint64_t vector, uint64_t err)
{
	serial_puts("trap ");
	serial_hex(vector);
	serial_puts(" err ");
	serial_hex(err);
	if (vector == 14) {
		uint64_t cr2;
		__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
		serial_puts(" cr2 ");
		serial_hex(cr2);
	}
	serial_putc('\n');
	panic("exception");
}
