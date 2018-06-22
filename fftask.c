/* 包含头文件 */
#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include "fftask.h"

/* 预编译开关 */
#define FOR_REAL_DOS  0

/* 该宏用于消除变量未使用的警告 */
#define DO_USE_VAR(var)  do { var = var; } while (0)

/* 内部常量定义 */
#define INTERRUPT_OFF()  do { disable(); } while (0)  /* 关中断 */
#define INTERRUPT_ON()   do { enable (); } while (0)  /* 开中断 */

#define KOBJ_TYPE_TASK     0
#define KOBJ_TYPE_MUTEX    1
#define KOBJ_TYPE_EVENT    2
#define KOBJ_TYPE_SEM      3
#define KOBJ_TYPE_MASK     0x7

#define KOBJ_TASK_DONE       (1 << 3)
#define KOBJ_EVENT_VALUE     (1 << 3)
#define KOBJ_EVENT_AUTORESET (1 << 4)
#define KOBJ_EVENT_WAKESALL  (1 << 5)

#define KOBJ_COMMON_MEMBERS \
    struct tagTASKCTRLBLK *o_next; \
    struct tagTASKCTRLBLK *o_prev; \
    struct tagTASKCTRLBLK *w_head; \
    struct tagTASKCTRLBLK *w_tail; \
    int    o_type; \
    int    o_data;

/* 默认任务堆栈大小 */
#define TASK_STACK_SIZE  1024

/* 内部类型定义 */
/* 任务控制块类型定义 */
typedef struct tagTASKCTRLBLK {
    KOBJ_COMMON_MEMBERS
    int t_timeout; /* 任务休眠超时 */
    int t_retv;    /* 任务返回值 */
    int t_ss;      /* 任务堆栈 ss */
    int t_sp;      /* 任务堆栈 sp */
    int t_stack[0];/* 任务堆栈 */
} TASKCTRLBLK;     /* 任务控制块 */

typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
} KERNELOBJ;

/* 内部全局变量定义 */
static void interrupt (*old_int_1ch)(void); /* 用于保存旧的时钟中断 */

static TASKCTRLBLK  maintask        = {0};  /* 主任务 */
static TASKCTRLBLK  ready_list_head = {0};  /* 任务就绪队列头 */
static TASKCTRLBLK  ready_list_tail = {0};  /* 任务就绪队列尾 */
static TASKCTRLBLK  sleep_list_head = {0};  /* 任务休眠队列头 */
static TASKCTRLBLK  sleep_list_tail = {0};  /* 任务休眠队列尾 */
static TASKCTRLBLK *g_running_task  = NULL; /* 当前运行的任务 */
static TASKCTRLBLK *g_prevtask      = NULL; /* 任务切换的前一个任务 */
static TASKCTRLBLK *g_nexttask      = NULL; /* 任务切换的下一个任务 */
static TASKCTRLBLK *g_idletask      = NULL; /* 指向空闲任务控制块 */

unsigned long g_tick_counter = 1;  /* 该变量用于记录系统 tick 次数 */
unsigned long g_idle_counter = 1;  /* 该变量用于记录空闲任务次数 */

/* 内部函数实现 */
/* ++ 任务队列管理函数 ++ */
/* 就绪队列入队 */
static void readyenqueue(TASKCTRLBLK *ptask)
{
    if (ptask == g_idletask) return;
    ptask->o_prev =  ready_list_tail.o_prev;
    ptask->o_next = &ready_list_tail;
    ptask->o_prev->o_next = ptask;
    ptask->o_next->o_prev = ptask;
}

/* 就绪队列出队 */
static TASKCTRLBLK* readydequeue(void)
{
    TASKCTRLBLK *ptask = ready_list_head.o_next;
    if (ptask != &ready_list_tail) {
        ptask->o_prev->o_next = ptask->o_next;
        ptask->o_next->o_prev = ptask->o_prev;
        return ptask;
    } else {
        return g_idletask;
    }
}

/* 休眠队列入队 */
static void sleepenqueue(TASKCTRLBLK *ptask)
{
    ptask->o_prev =  sleep_list_tail.o_prev;
    ptask->o_next = &sleep_list_tail;
    ptask->o_prev->o_next = ptask;
    ptask->o_next->o_prev = ptask;
}

static TASKCTRLBLK* waitdequeue(KERNELOBJ *kobj)
{
    TASKCTRLBLK *ptask = kobj->w_head;
    kobj->w_head = kobj->w_head->o_next;
    if (kobj->w_head) {
        kobj->w_head->o_prev = NULL;
    } else {
        kobj->w_tail = NULL;
    }
    return ptask;
}

static void waitenqueue(KERNELOBJ *kobj, TASKCTRLBLK *ptask, int timeout)
{
    ptask->t_timeout = timeout;
    ptask->o_next    = NULL;
    ptask->o_prev    = ptask->w_tail;
    if (kobj->w_tail) {
        kobj->w_tail->o_next = ptask;
    }
    kobj->w_tail = ptask;
    if (kobj->w_head == NULL) {
        kobj->w_head = ptask;
    }
}

static void makeallwaitready(KERNELOBJ *kobj)
{
    if (kobj->w_head) {
        kobj->w_tail->o_next =  ready_list_head.o_next;
        ready_list_head.o_next->o_prev = kobj->w_tail;
        ready_list_head.o_next = kobj->w_head;
        kobj->w_head->o_prev = &ready_list_head;
        kobj->w_head = kobj->w_tail = NULL;
    }
}
/* -- 任务队列管理函数 -- */

/* 处理休眠队列 */
static void handle_sleep_task(void)
{
    TASKCTRLBLK *ptask = sleep_list_tail.o_prev;
    TASKCTRLBLK *pready;

    /* 从尾至头遍历休眠队列 */
    while (ptask != &sleep_list_head) {
        pready = ptask;
        ptask  = ptask->o_prev;

        /* 判断休眠是否完成 */
        if (--pready->t_timeout == 0) {
            /* 将 pready 从休眠队列中删除 */
            pready->o_next->o_prev = pready->o_prev;
            pready->o_prev->o_next = pready->o_next;

            /* 将 pready 加入就绪队列头 */
            pready->o_next =  ready_list_head.o_next;
            pready->o_prev = &ready_list_head;
            pready->o_prev->o_next = pready;
            pready->o_next->o_prev = pready;
        }
    }
}

static void interrupt new_int_1ch(void)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* system tick counter */
    g_tick_counter++;

    /* 当前运行的任务放入就绪队列尾部 */
    g_prevtask = g_running_task;
    readyenqueue(g_running_task);

    /* 处理休眠队列 */
    handle_sleep_task();

    /* 取出就绪任务 */
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;
    g_running_task = g_nexttask;
    g_idle_counter+=(g_running_task == g_idletask);

    /* 开中断 */
    INTERRUPT_ON();

    /* 清除中断屏蔽 */
    outp(0x20, 0x20);
}

static void interrupt switch_task(void)
{
    /* 进行任务切换 */
    if (g_prevtask) {
        g_prevtask->t_ss = _SS;
        g_prevtask->t_sp = _SP;
    }
    if (g_nexttask) {
        _SS = g_nexttask->t_ss;
        _SP = g_nexttask->t_sp;
        g_running_task = g_nexttask;
    }
    /* 开中断 */
    INTERRUPT_ON();
}

/* 任务运行结束的处理函数 */
static void far task_done_handler(TASKCTRLBLK far *ptask)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* 保存任务返回值 */
    ptask->t_retv = _AX;

    /* 标记该任务运行结束 */
    ptask->o_type |= KOBJ_TASK_DONE;

    /* 将所有等待 ptask 的任务放入就绪队列头 */
    if (ptask->w_head) {
        makeallwaitready((KERNELOBJ*)ptask);
    }

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();
}

/* 系统空闲任务 */
static int far idle_task_proc(void far *p)
{
    /* 消除编译警告 */
    DO_USE_VAR(p);
    while (1) {
        /* 释放处理器资源 */
#if FOR_REAL_DOS
        asm hlt;
#else
        asm mov ax, 0x1680;
        asm int 0x2f;
#endif
    }
    /* return 0; */
}

/* 函数实现 */
int ffkernel_init(void)
{
    int ret = -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 初始化任务队列 */
    /* 就绪队列 */
    ready_list_head.o_next = &ready_list_tail;
    ready_list_head.o_prev = &ready_list_tail;
    ready_list_tail.o_next = &ready_list_head;
    ready_list_tail.o_prev = &ready_list_head;

    /* 休眠队列 */
    sleep_list_head.o_next = &sleep_list_tail;
    sleep_list_head.o_prev = &sleep_list_tail;
    sleep_list_tail.o_next = &sleep_list_head;
    sleep_list_tail.o_prev = &sleep_list_head;

    /* 初始化任务指针 */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* 创建空闲任务 */
    g_idletask = task_create(idle_task_proc, NULL, 256);
    if (!g_idletask) goto done;

    /* 将空闲任务从就绪队列中删除 */
    g_idletask->o_next->o_prev = g_idletask->o_prev;
    g_idletask->o_prev->o_next = g_idletask->o_next;

    /* 初始化 int 1ch 中断 */
    old_int_1ch = getvect(0x1c);
    setvect(0x1c, new_int_1ch);

    /* set to 100Hz freq */
    #define _8253_FREQ     1193181L
    #define _8253_COUNTER  11932L
    outportb(0x43, 0x3c);
    outportb(0x40, (_8253_COUNTER >> 0) & 0xff);
    outportb(0x40, (_8253_COUNTER >> 8) & 0xff);
    ret = 0;

done:
    /* 开中断 */
    INTERRUPT_ON();
    return ret;
}

void ffkernel_exit(void)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* restore int 1ch */
    /* 恢复时钟中断 */
    setvect (0x1c, old_int_1ch);
    outportb(0x43, 0x3c);
    outportb(0x40, 0x00);
    outportb(0x40, 0x00);

    /* 关闭空闲任务 */
    task_destroy(g_idletask);

    /* 切换到主任务 */
    g_prevtask = g_running_task;
    g_nexttask = &maintask;

    /* 进行任务切换 */
    switch_task();
}

void* task_create(TASK task, void far *p, int size)
{
    TASKCTRLBLK *ptask = NULL;
    int         *stack = NULL;

    /* 参数有效性检查 */
    if (!task) return 0;
    if (!size) size = TASK_STACK_SIZE;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 分配任务控制块 */
    ptask = malloc(sizeof(TASKCTRLBLK) + size);
    if (!ptask) goto done;

    /* 任务控制块清零 */
    memset(ptask, 0, sizeof(TASKCTRLBLK));

    /* 指向任务栈顶 */
    stack = (ptask->t_stack + size);

    /* 任务控制块地址入栈 */
    *--stack = FP_SEG(ptask);
    *--stack = FP_OFF(ptask);

    /* 任务参数 p 入栈 */
    *--stack = FP_SEG(p);
    *--stack = FP_OFF(p);

    /* 任务结束处理函数地址入栈 */
    *--stack = FP_SEG(task_done_handler);
    *--stack = FP_OFF(task_done_handler);

    /* 初始化任务堆栈 */
    *--stack = 0x0200;       /* flag */
    *--stack = FP_SEG(task); /* cs */
    *--stack = FP_OFF(task); /* ip */
    *--stack = 0;            /* ax */
    *--stack = 0;            /* bx */
    *--stack = 0;            /* cx */
    *--stack = 0;            /* dx */
    *--stack = 0;            /* es */
    *--stack =_DS;           /* ds */
    *--stack = 0;            /* si */
    *--stack = 0;            /* di */
    *--stack = 0;            /* bp */

    /* 保存任务堆栈入口 */
    ptask->t_ss = FP_SEG(stack);
    ptask->t_sp = FP_OFF(stack);

    /* 加入就绪队列 */
    readyenqueue(ptask);

done:
    /* 开中断 */
    INTERRUPT_ON();

    /* 返回任务句柄 */
    return ptask;
}

int task_destroy(void *htask)
{
    TASKCTRLBLK *ptask = htask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 欲挂销毁任务为当前运行任务 */
    if (g_running_task == ptask) {
        free(htask);
        g_prevtask = NULL;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* 欲挂起的任务不为当前运行任务 */
        if (ptask->o_next) ptask->o_next->o_prev = ptask->o_prev;
        if (ptask->o_prev) ptask->o_prev->o_next = ptask->o_next;
        free(htask);
        INTERRUPT_ON();
    }

    return 0;
}

int task_suspend(void *htask)
{
    TASKCTRLBLK *ptask = htask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 欲挂起的任务为当前运行任务 */
    if (g_running_task == ptask) {
        /* 取出就绪任务 */
        g_prevtask = g_running_task;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* 欲挂起的任务不为当前运行任务 */
        if (ptask->o_next) ptask->o_next->o_prev = ptask->o_prev;
        if (ptask->o_prev) ptask->o_prev->o_next = ptask->o_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_resume(void *htask)
{
    TASKCTRLBLK *ptask = htask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 如果欲恢复的任务不在当前运行状态 */
    if (g_running_task != ptask) {
        /* 将当前运行任务放入就绪队列尾 */
        g_prevtask = g_running_task;
        readyenqueue(g_prevtask);

        /* ptch 作为当前任务 */
        g_nexttask = ptask;

        /* 进行任务切换 */
        switch_task();
    } else {
        /* 开中断 */
        INTERRUPT_ON();
    }
    return 0;
}

/* 任务休眠 */
int task_sleep(int ms)
{
    /* ms 为零直接返回 */
    if (ms <= 0) return 0;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 设置任务休眠时间 */
    g_running_task->t_timeout = (ms + 9) / 10;

    /* 将当前运行的任务放入休眠队列尾 */
    sleepenqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int task_wait(void *htask, int timeout)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)htask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return 0;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 将当前任务放入 ptask 的等待队列尾 */
    waitenqueue((KERNELOBJ*)ptask, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int task_exitcode(void *htask, int *code)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)htask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务未运行结束 */
    if ((ptask->o_type & KOBJ_TASK_DONE) == 0) return -1;

    /* 返回结束码 */
    if (code) *code = ptask->t_retv;

    return 0;
}

/* 创建互斥体 */
void* mutex_create(void)
{
    KERNELOBJ *pmutex;

    /* 为互斥体分配缓冲区 */
    pmutex = calloc(1, sizeof(KERNELOBJ));
    if (!pmutex) return 0;

    /* 初始化互斥体 */
    pmutex->o_type = KOBJ_TYPE_MUTEX;
    return pmutex;
}

/* 销毁互斥体 */
int mutex_destroy(void *hmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* 参数有效性检查 */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* 释放互斥体件缓冲区 */
    if (pmutex) free(pmutex);
    return 0;
}

int mutex_lock(void *hmutex, int timeout)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* 参数有效性检查 */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 互斥体加锁 */
    if (pmutex->o_data++ == 0) {
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前任务放入 pmutex 的等待队列尾 */
    waitenqueue(pmutex, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int mutex_unlock(void *hmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* 参数有效性检查 */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 互斥体解锁 */
    if (pmutex->o_data > 0) pmutex->o_data--;

    /* 如果没有任务等待该互斥体则返回 */
    if (!pmutex->w_head) {
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前运行任务放入就绪队列尾 */
    g_prevtask = g_running_task;
    readyenqueue(g_running_task);

    /* 将第一个等待 pmutex 的任务取出 */
    g_nexttask = waitdequeue(pmutex);

    /* 进行任务切换 */
    switch_task();
    return 0;
}

void* event_create(int flags)
{
    KERNELOBJ *pevent;

    /* 为 event 分配缓冲区 */
    pevent = calloc(1, sizeof(KERNELOBJ));
    if (!pevent) return 0;

    /* 初始化 event */
    pevent->o_type = KOBJ_TYPE_EVENT | (flags & 0xf8);
    return pevent;
}

int event_destroy(void *hevent)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* 参数有效性检查 */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* 释放互斥体件缓冲区 */
    if (pevent) free(pevent);
}

int event_wait(void *hevent, int timeout)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* 参数有效性检查 */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* 关中断 */
    INTERRUPT_OFF();
    if (pevent->o_type & KOBJ_EVENT_VALUE) {
        if (pevent->o_type & KOBJ_EVENT_AUTORESET) {
            pevent->o_type &= ~KOBJ_EVENT_VALUE;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前任务放入 pevent 的等待队列尾 */
    waitenqueue(pevent, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();

    if (pevent->o_type & KOBJ_EVENT_AUTORESET) {
        pevent->o_type &= ~KOBJ_EVENT_VALUE;
    }
}

int event_setval(void *hevent, int value)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* 参数有效性检查 */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    pevent->o_type &= ~KOBJ_EVENT_VALUE;
    pevent->o_type |=  value ? KOBJ_EVENT_VALUE : 0;

    /* 如果没有任务等待该互斥体则返回 */
    if (!value || !pevent->w_head) {
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前运行任务放入就绪队列尾 */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

    if (pevent->o_type & KOBJ_EVENT_WAKESALL) {
        /* 将所有等待 pevent 的任务放入就绪队列头 */
        makeallwaitready(pevent);
        g_nexttask = readydequeue(); /* 再从就绪队列取出任务 */
    } else {
        /* 将第一个等待 pevent 的任务取出 */
        g_nexttask = waitdequeue(pevent);
    }

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int event_getval(void *hevent, int *value)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* 参数有效性检查 */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* 返回 event 值 */
    *value = (pevent->o_type & KOBJ_EVENT_VALUE) ? 1 : 0;
    return 0;
}

