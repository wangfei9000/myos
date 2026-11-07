#include "kernel.h"

/*
 * 一位一页。位为 1 表示已占用。位图按 256MB 预留，实际可用结尾由
 * Multiboot 的 mem_upper 决定，并且不超过已经恒等映射的 1GB。
 */
#define PAGE 4096UL
#define MAX_BYTES (256UL * 1024 * 1024)
#define MAX_FRAMES (MAX_BYTES / PAGE)

static uint8_t bitmap[MAX_FRAMES / 8];
static uint64_t frame_count;
static uint64_t mem_end;

static void bit_set(uint64_t frame)
{
	bitmap[frame >> 3] |= (uint8_t)(1u << (frame & 7));
}

static void bit_clear(uint64_t frame)
{
	bitmap[frame >> 3] &= (uint8_t)~(1u << (frame & 7));
}

static int bit_test(uint64_t frame)
{
	return (bitmap[frame >> 3] >> (frame & 7)) & 1;
}

void pmm_init(void)
{
	uint64_t end = 128UL * 1024 * 1024;

	if (multiboot_magic == 0x2BADB002 && multiboot_info_phys != 0) {
		uint32_t flags = *(uint32_t *)(uint64_t)multiboot_info_phys;
		if (flags & 1) {
			uint32_t upper = *(uint32_t *)(uint64_t)(multiboot_info_phys + 8);
			end = 1024UL * 1024 + (uint64_t)upper * 1024;
		}
	}
	if (end > 1024UL * 1024 * 1024)
		end = 1024UL * 1024 * 1024;
	end &= ~(PAGE - 1);
	mem_end = end;
	frame_count = end / PAGE;

	for (uint64_t i = 0; i < sizeof bitmap; i++)
		bitmap[i] = 0xff;

	uint64_t start = ((uint64_t)stack_top + PAGE - 1) & ~(PAGE - 1);
	for (uint64_t addr = start; addr + PAGE <= end; addr += PAGE)
		bit_clear(addr / PAGE);
}

uint64_t pmm_alloc(void)
{
	for (uint64_t frame = 0; frame < frame_count; frame++) {
		if (bit_test(frame))
			continue;
		bit_set(frame);
		return frame * PAGE;
	}
	return 0;
}

void pmm_free(uint64_t phys)
{
	if (phys >= mem_end || (phys & (PAGE - 1)) != 0)
		return;
	bit_clear(phys / PAGE);
}
