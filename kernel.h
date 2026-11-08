#pragma once

#include <stdint.h>

/* 向 COM1 写一个字符。 */
void serial_putc(char c);
/* 向 COM1 逐字节写字符串。 */
void serial_puts(const char *s);
/* 向 COM1 输出 0x 和 16 位十六进制数。 */
void serial_hex(uint64_t value);
/* 串口打印 panic 消息后关中断并停机。 */
void panic(const char *msg);

/* 向 80x25 文字屏幕写一个字符。 */
void vga_putc(char c);
/* 向文字屏幕逐字符写字符串。 */
void vga_puts(const char *s);
/* 清空文字屏幕，光标回到左上角。 */
void vga_clear(void);
/* 把第 0 行写成状态栏，正文从第 1 行继续。 */
void vga_status(const char *s);

/* 读取一个按下的键，没有键时返回 -1。 */
int kbd_poll(void);

/* 帮助页。按 q 回到菜单。 */
void help_main(void);
/* 连续输出 A。按 q 回到菜单。 */
void thread_a_main(void);
/* 连续输出 B。按 q 回到菜单。 */
void thread_b_main(void);

/* 用 Multiboot 信息建立物理页位图。 */
void pmm_init(void);
/* 分配一页物理内存，没有空闲页时返回 0。 */
uint64_t pmm_alloc(void);
/* 释放一页物理内存。 */
void pmm_free(uint64_t phys);

/* 清空任务表，此时还没有可运行任务。 */
void task_init(void);
/* 创建一个内核任务，分配一页栈并标为 READY。 */
void task_create(void (*fn)(void));
/* 按轮转挑选任务并切换，调用后不再返回。 */
void scheduler(void);
/* 当前任务让出 CPU；正在调度器栈上时直接返回。 */
void yield_if_task(void);

/* 装入异常门和时钟门，然后执行 lidt。 */
void idt_init(void);
/* 重映射 8259，只放行时钟 IRQ0。 */
void pic_init(void);
/* 把 PIT 设成约 100Hz。 */
void pit_init(void);
/* 打印异常向量和错误码，页错误再打印 CR2，然后停机。 */
void trap_handler(uint64_t vector, uint64_t err);

/* boot.S 在 cpuid 之前写入。物理地址都低于 4GB。 */
extern uint32_t multiboot_magic;
extern uint32_t multiboot_info_phys;

/* 链接脚本里的启动栈顶，也是内核占用区的结尾。 */
extern char stack_top[];
