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

/* 清空 8 个任务槽。此时没有正在运行的任务，轮转从 0 号槽开始。 */
void task_init(void)
{
	current = 0;
	rr_index = 0;
	for (int i = 0; i < NTASK; i++)
		tasks[i].state = UNUSED;
}

/*
 * 占用一个空槽，分配一页当作内核栈，并写成 swtch 可以直接恢复的样子。
 * 新任务先标成 READY，要等 scheduler 挑中才开始执行 fn。
 * 没有空槽或没有空闲页时停机。
 */
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
	 * page 是这一页的低地址，page + 4096 是页尾之后的第一个地址。
	 * 栈向低地址生长，所以从页尾往下写。swtch 按 r15…rbp 弹出，再 ret。
	 * 第一次切过来时 ret 进入 task_entry，task_entry 再弹出 fn 并调用。
	 */
	uint64_t *sp = (uint64_t *)(page + 4096);
	*--sp = (uint64_t)fn;          /* task_entry 弹出的函数指针 */
	*--sp = (uint64_t)task_entry;  /* swtch 的 ret 目标 */
	*--sp = 0;                     /* rbp */
	*--sp = 0;                     /* rbx */
	*--sp = 0;                     /* r12 */
	*--sp = 0;                     /* r13 */
	*--sp = 0;                     /* r14 */
	*--sp = 0;                     /* r15，swtch 第一个弹出 */

	t->stack_page = (uint8_t *)page;
	t->rsp = (uint64_t)sp;
	t->state = READY;
}

/* 从上次选中的下一槽开始绕一圈，返回第一个 READY。都没有则返回空。 */
static struct task *pick_next(void)
{
	for (int n = 0; n < NTASK; n++) {
		rr_index = (rr_index + 1) & (NTASK - 1);
		if (tasks[rr_index].state == READY)
			return &tasks[rr_index];
	}
	return 0;
}

/*
 * 时钟中断在任务栈上调用。把当前任务改回 READY，swtch 回调度器栈。
 * current 为空表示此刻正在调度器里，不能再换栈，直接返回。
 */
void yield_if_task(void)
{
	if (!current)
		return;
	current->state = READY;
	swtch(&current->rsp, &sched_rsp);
}

/*
 * 不再返回。挑到任务就关中断并换到它的栈；没有 READY 就开中断后 hlt，
 * 等下一次时钟再挑。从任务 yield 回来后 current 清零。
 */
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
