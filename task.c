#include "kernel.h"

enum {
	UNUSED = 0,
	READY,
	RUNNING
};

struct task {
	int state;
	uint64_t rsp;
	uint8_t *stack_page;
};

#define NTASK 8

static struct task tasks[NTASK];
static int rr_index;
static uint64_t sched_rsp;

struct task *current;

extern void swtch(uint64_t *save_rsp, uint64_t *load_rsp);
extern void task_entry(void);

void task_init(void)
{
	current = 0;
	rr_index = 0;
	for (int i = 0; i < NTASK; i++)
		tasks[i].state = UNUSED;
}

void task_create(void (*fn)(void))
{
	struct task *t = 0;
	for (int i = 0; i < NTASK; i++) {
		if (tasks[i].state == UNUSED) {
			t = &tasks[i];
			break;
		}
	}
	if (!t)
		panic("no task slot");

	uint64_t page = pmm_alloc();
	if (!page)
		panic("no stack page");

	/*
	 * 栈顶先放函数指针，再放一份刚被 swtch 保存过的样子。
	 * 第一次切过来时，ret 进入 task_entry，再弹出函数指针。
	 */
	uint64_t *sp = (uint64_t *)(page + 4096);
	*--sp = (uint64_t)fn;
	*--sp = (uint64_t)task_entry;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;
	*--sp = 0;

	t->stack_page = (uint8_t *)page;
	t->rsp = (uint64_t)sp;
	t->state = READY;
}

static struct task *pick_next(void)
{
	for (int n = 0; n < NTASK; n++) {
		rr_index = (rr_index + 1) & (NTASK - 1);
		if (tasks[rr_index].state == READY)
			return &tasks[rr_index];
	}
	return 0;
}

void yield_if_task(void)
{
	if (!current)
		return;
	current->state = READY;
	swtch(&current->rsp, &sched_rsp);
}

void scheduler(void)
{
	for (;;) {
		struct task *t = pick_next();
		if (!t) {
			__asm__ volatile("sti; hlt; cli");
			continue;
		}
		/*
		 * 切到任务栈之前关掉中断。current 非空时，时钟处理会调用
		 * yield；那时必须已经在任务自己的栈上。
		 */
		__asm__ volatile("cli");
		t->state = RUNNING;
		current = t;
		swtch(&sched_rsp, &t->rsp);
		current = 0;
	}
}
