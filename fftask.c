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
#define KOBJ_TYPE_SEM      2
#define KOBJ_TYPE_EVENT    3
#define KOBJ_TYPE_MASK     0x7
#define KOBJ_TASK_DONE    (1 << 3)

#define KOBJ_COMMON_MEMBERS \
    struct tagTASKCTRLBLK *o_next; \
    struct tagTASKCTRLBLK *o_prev; \
    struct tagTASKCTRLBLK *w_head; \
    struct tagTASKCTRLBLK *w_tail; \
    int    o_type;

/* 默认任务堆栈大小 */
#define TASK_STACK_SIZE  1024

/* 内部类型定义 */
/* 任务控制块类型定义 */
typedef struct tagTASKCTRLBLK {
    KOBJ_COMMON_MEMBERS
    struct tagTASKCTRLBLK *t_next;
    struct tagTASKCTRLBLK *t_prev;
    int t_timeout; /* 任务休眠超时 */
    int t_retv;    /* 任务返回值 */
    int t_ss;      /* 任务堆栈 ss */
    int t_sp;      /* 任务堆栈 sp */
    int t_stack[0];/* 任务堆栈 */
} TASKCTRLBLK;     /* 任务控制块 */

typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
    int   o_data0;
    int   o_data1;
    void *o_owner;
} KERNELOBJ;

/* 内部全局变量定义 */
static void interrupt (*old_int_1ch)(void); /* 用于保存旧的时钟中断 */

static TASKCTRLBLK  maintask        = {0};  /* 主任务 */
static TASKCTRLBLK  ready_list_head = {0};  /* 任务就绪队列头 */
static TASKCTRLBLK  ready_list_tail = {0};  /* 任务就绪队列尾 */
static TASKCTRLBLK  sleep_list_head = {0};  /* 任务休眠队列头 */
static TASKCTRLBLK  sleep_list_tail = {0};  /* 任务休眠队列尾 */
static KERNELOBJ    kobj_list_head  = {0};  /* 内核对象列表头 */
static KERNELOBJ    kobj_list_tail  = {0};  /* 内核对象列表尾 */
static TASKCTRLBLK *g_running_task  = NULL; /* 当前运行的任务 */
static TASKCTRLBLK *g_prevtask      = NULL; /* 任务切换的前一个任务 */
static TASKCTRLBLK *g_nexttask      = NULL; /* 任务切换的下一个任务 */
static char         g_idletask[256];        /* 空闲任务控制块 */

unsigned long g_tick_counter = 1;  /* 该变量用于记录系统 tick 次数 */
unsigned long g_idle_counter = 1;  /* 该变量用于记录空闲任务次数 */

/* 内部函数实现 */
/* ++ 任务队列管理函数 ++ */
/* 就绪队列入队 */
static void readyenqueue(TASKCTRLBLK *ptask)
{
    if (ptask == (TASKCTRLBLK*)g_idletask) return;
    ptask->t_prev =  ready_list_tail.t_prev;
    ptask->t_next = &ready_list_tail;
    ptask->t_prev->t_next = ptask;
    ptask->t_next->t_prev = ptask;
}

/* 就绪队列出队 */
static TASKCTRLBLK* readydequeue(void)
{
    TASKCTRLBLK *ptask = ready_list_head.t_next;
    if (ptask != &ready_list_tail) {
        ptask->t_prev->t_next = ptask->t_next;
        ptask->t_next->t_prev = ptask->t_prev;
        return ptask;
    } else {
        return (TASKCTRLBLK*)g_idletask;
    }
}

/* 休眠队列入队 */
static void sleepenqueue(TASKCTRLBLK *ptask)
{
    ptask->t_prev =  sleep_list_tail.t_prev;
    ptask->t_next = &sleep_list_tail;
    ptask->t_prev->t_next = ptask;
    ptask->t_next->t_prev = ptask;
}

static TASKCTRLBLK* waitdequeue(KERNELOBJ *kobj)
{
    TASKCTRLBLK *ptask = kobj->w_head;
    kobj->w_head = kobj->w_head->t_next;
    if (kobj->w_head) {
        kobj->w_head->t_prev = NULL;
    } else {
        kobj->w_tail = NULL;
    }
    return ptask;
}

static void waitenqueue(KERNELOBJ *kobj, TASKCTRLBLK *ptask, int timeout)
{
    ptask->t_timeout = timeout;
    ptask->t_next    = NULL;
    ptask->t_prev    = ptask->w_tail;
    if (kobj->w_tail) {
        kobj->w_tail->t_next = ptask;
    }
    kobj->w_tail = ptask;
    if (kobj->w_head == NULL) {
        kobj->w_head = ptask;
    }
}
/* -- 任务队列管理函数 -- */

/* 处理休眠队列 */
static void handle_sleep_task(void)
{
    TASKCTRLBLK *ptask = sleep_list_tail.t_prev;
    TASKCTRLBLK *pready;

    /* 从尾至头遍历休眠队列 */
    while (ptask != &sleep_list_head) {
        pready = ptask;
        ptask  = ptask->t_prev;

        /* 判断休眠是否完成 */
        if (--pready->t_timeout == 0) {
            /* 将 pready 从休眠队列中删除 */
            pready->t_next->t_prev = pready->t_prev;
            pready->t_prev->t_next = pready->t_next;

            /* 将 pready 加入就绪队列头 */
            pready->t_next =  ready_list_head.t_next;
            pready->t_prev = &ready_list_head;
            pready->t_prev->t_next = pready;
            pready->t_next->t_prev = pready;
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
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

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
    g_idle_counter+=(g_running_task == (TASKCTRLBLK*)g_idletask);

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
        ptask->w_tail->t_next =  ready_list_head.t_next;
        ready_list_head.t_next->t_prev = ptask->w_tail;
        ready_list_head.t_next = ptask->w_head;
        ptask->w_head->t_prev = &ready_list_head;
        ptask->w_head = ptask->w_tail = NULL;
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
void ffkernel_init(void)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* 初始化就绪队列 */
    ready_list_head.t_next = &ready_list_tail;
    ready_list_head.t_prev = &ready_list_tail;
    ready_list_tail.t_next = &ready_list_head;
    ready_list_tail.t_prev = &ready_list_head;

    /* 初始化休眠队列 */
    sleep_list_head.t_next = &sleep_list_tail;
    sleep_list_head.t_prev = &sleep_list_tail;
    sleep_list_tail.t_next = &sleep_list_head;
    sleep_list_tail.t_prev = &sleep_list_head;

    /* 初始化任务指针 */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* 创建空闲任务 */
    task_create(idle_task_proc, NULL, g_idletask, sizeof(g_idletask));

    /* 将空闲任务从就绪队列中删除 */
    ((TASKCTRLBLK*)g_idletask)->t_next->t_prev = ((TASKCTRLBLK*)g_idletask)->t_prev;
    ((TASKCTRLBLK*)g_idletask)->t_prev->t_next = ((TASKCTRLBLK*)g_idletask)->t_next;

    /* 初始化 int 1ch 中断 */
    old_int_1ch = getvect(0x1c);
    setvect(0x1c, new_int_1ch);

    /* set to 100Hz freq */
    #define _8253_FREQ     1193181L
    #define _8253_COUNTER  11932L
    outportb(0x43, 0x3c);
    outportb(0x40, (_8253_COUNTER >> 0) & 0xff);
    outportb(0x40, (_8253_COUNTER >> 8) & 0xff);

    /* 开中断 */
    INTERRUPT_ON();
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

int task_create(TASK taskfunc, void far *taskparam, void *ctask, int size)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;
    int         *stack = NULL;

    /* 参数有效性检查 */
    if (!ctask || size < 256) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 任务控制块清零 */
    memset(ptask, 0, sizeof(TASKCTRLBLK));

    /* 指向任务栈顶 */
    stack = (ptask->t_stack + size - sizeof(TASKCTRLBLK));

    /* 任务控制块地址入栈 */
    *--stack = FP_SEG(ptask);
    *--stack = FP_OFF(ptask);

    /* 任务参数 p 入栈 */
    *--stack = FP_SEG(taskparam);
    *--stack = FP_OFF(taskparam);

    /* 任务结束处理函数地址入栈 */
    *--stack = FP_SEG(task_done_handler);
    *--stack = FP_OFF(task_done_handler);

    /* 初始化任务堆栈 */
    *--stack = 0x0200;           /* flag */
    *--stack = FP_SEG(taskfunc); /* cs */
    *--stack = FP_OFF(taskfunc); /* ip */
    *--stack = 0;                /* ax */
    *--stack = 0;                /* bx */
    *--stack = 0;                /* cx */
    *--stack = 0;                /* dx */
    *--stack = 0;                /* es */
    *--stack =_DS;               /* ds */
    *--stack = 0;                /* si */
    *--stack = 0;                /* di */
    *--stack = 0;                /* bp */

    /* 保存任务堆栈入口 */
    ptask->t_ss = FP_SEG(stack);
    ptask->t_sp = FP_OFF(stack);

    /* 加入就绪队列 */
    readyenqueue(ptask);

    /* 开中断 */
    INTERRUPT_ON();
    return 0;
}

int task_destroy(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    if (g_running_task == ptask) { /* 欲销毁任务为当前运行任务 */
        g_prevtask = NULL;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* 欲销毁任务不为当前运行任务 */
        if (ptask->t_next) ptask->t_next->t_prev = ptask->t_prev;
        if (ptask->t_prev) ptask->t_prev->t_next = ptask->t_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_suspend(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return -2;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 欲挂起的任务为当前运行任务 */
    if (g_running_task == ptask) {
        /* 取出就绪任务 */
        g_prevtask = g_running_task;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* 欲挂起的任务不为当前运行任务 */
        if (ptask->t_next) ptask->t_next->t_prev = ptask->t_prev;
        if (ptask->t_prev) ptask->t_prev->t_next = ptask->t_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_resume(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return -2;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 如果欲恢复的任务不在当前运行状态 */
    if (g_running_task != ptask) {
        /* 将当前运行任务放入就绪队列尾 */
        readyenqueue(g_running_task);
        g_prevtask = g_running_task;

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

int task_wait(void *ctask, int timeout)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;

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

int task_exitcode(void *ctask, int *code)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务未运行结束 */
    if ((ptask->o_type & KOBJ_TASK_DONE) == 0) return -2;

    /* 返回结束码 */
    if (code) *code = ptask->t_retv;
    return 0;
}

/* 创建互斥体 */
int mutex_create(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;
    if (!cmutex) return -1;

    /* 初始化互斥体 */
    pmutex->o_type  = KOBJ_TYPE_MUTEX;
    pmutex->o_data0 = 1;
    return 0;
}

/* 销毁互斥体 */
int mutex_destroy(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    return 0;
}

int mutex_lock(void *cmutex, int timeout)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 互斥体加锁 */
    if (pmutex->o_data0 > 0) {
        pmutex->o_data0--;
        pmutex->o_owner = g_running_task;
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

    INTERRUPT_OFF();
    pmutex->o_owner = g_running_task;
    INTERRUPT_ON();
    return 0;
}

int mutex_unlock(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 当前任务不是 mutex 的所有者 */
    if (pmutex->o_owner != g_running_task) {
        INTERRUPT_ON();
        return -2;
    }

    /* 如果没有任务等待该互斥体则返回 */
    if (!pmutex->w_head) {
        if (pmutex->o_data0 < 1) {
            pmutex->o_data0++;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前运行任务放入就绪队列尾 */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

    /* 将第一个等待 pmutex 的任务取出 */
    g_nexttask = waitdequeue(pmutex);

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int sem_create(void *csem, int initval, int maxval)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;
    if (!csem) return -1;

    /* 初始化 event */
    psem->o_type  = KOBJ_TYPE_SEM;
    psem->o_data0 = initval;
    psem->o_data1 = maxval ;
    return 0;
}

int sem_destroy(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    return 0;
}

int sem_wait(void *csem, int timeout)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 如果信号量大于零 */
    if (psem->o_data0 > 0) {
        psem->o_data0--;
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前任务放入 psem 的等待队列尾 */
    waitenqueue(psem, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* 进行任务切换 */
    switch_task();
}

int sem_post(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 如果没有任务等待该互斥体则返回 */
    if (!psem->w_head) {
        if (psem->o_data0 < psem->o_data1) {
            psem->o_data0++;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* 将当前运行任务放入就绪队列尾 */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = waitdequeue(psem); /* 将第一个等待 psem 的任务取出 */

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int sem_getval(void *csem, int *value)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* 返回 sem 值 */
    *value = psem->o_data1;
    return 0;
}

