#pragma once

#include <stdint.h>

void serial_putc(char c);
void serial_puts(const char *s);
void serial_hex(uint64_t value);
void panic(const char *msg);

void pmm_init(void);
uint64_t pmm_alloc(void);
void pmm_free(uint64_t phys);

void task_init(void);
void task_create(void (*fn)(void));
void scheduler(void);
void yield_if_task(void);

void idt_init(void);
void pic_init(void);
void pit_init(void);
void trap_handler(uint64_t vector, uint64_t err);

/* boot.S 在 cpuid 之前写入。物理地址都低于 4GB。 */
extern uint32_t multiboot_magic;
extern uint32_t multiboot_info_phys;

/* 链接脚本里的启动栈顶，也是内核占用区的结尾。 */
extern char stack_top[];
