/* 包含头文件 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
#define KOBJ_TASK_TIMEOUT (1 << 4)

/* 内部全局变量定义 */
static void interrupt (*old_int_1ch)(void); /* 用于保存旧的时钟中断 */

static TASKCTRLBLK  maintask        = {0};  /* 主任务 */
static TASKCTRLBLK  ready_list_head = {0};  /* 任务就绪队列头 */
static TASKCTRLBLK  ready_list_tail = {0};  /* 任务就绪队列尾 */
static TASKCTRLBLK  sleep_list_head = {0};  /* 任务休眠队列头 */
static TASKCTRLBLK  sleep_list_tail = {0};  /* 任务休眠队列尾 */
static KERNELOBJ    kobjs_list_head = {0};  /* 内核对象列表头 */
static KERNELOBJ    kobjs_list_tail = {0};  /* 内核对象列表尾 */
static char         g_idletask[256];        /* 空闲任务控制块 */

TASKCTRLBLK *g_running_task  = NULL; /* 当前运行的任务 */
TASKCTRLBLK *g_prevtask      = NULL; /* 任务切换的前一个任务 */
TASKCTRLBLK *g_nexttask      = NULL; /* 任务切换的下一个任务 */

unsigned long g_tick_counter = 1;  /* 该变量用于记录系统 tick 次数 */
unsigned long g_idle_counter = 1;  /* 该变量用于记录空闲任务次数 */

/* 内部函数实现 */
/* ++ 任务队列管理函数 ++ */
/* 就绪队列入队 */
static void ready_enqueue(TASKCTRLBLK *ptask)
{
    if (ptask == (TASKCTRLBLK*)g_idletask) return;
    ptask->t_prev =  ready_list_tail.t_prev;
    ptask->t_next = &ready_list_tail;
    ptask->t_prev->t_next = ptask;
    ptask->t_next->t_prev = ptask;
}

/* 就绪队列出队 */
static TASKCTRLBLK* ready_dequeue(void)
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

/* 等待队列入队 */
static void wait_enqueue(KERNELOBJ *kobj, TASKCTRLBLK *ptask, int timeout)
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

/* 等待队列出队 */
static TASKCTRLBLK* wait_dequeue(KERNELOBJ *kobj)
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

/* 将 kobj 加入内核对象列表 */
static void kobjs_append(KERNELOBJ *kobj)
{
    if (kobj == (KERNELOBJ*)g_idletask) return;
    kobj->o_prev =  kobjs_list_tail.o_prev;
    kobj->o_next = &kobjs_list_tail;
    kobj->o_prev->o_next = kobj;
    kobj->o_next->o_prev = kobj;
}

/* 将 kobj 从内核对象列表移除 */
static void kobjs_remove(KERNELOBJ *kobj)
{
    if (kobj == (KERNELOBJ*)g_idletask) return;
    kobj->o_next->o_prev = kobj->o_prev;
    kobj->o_prev->o_next = kobj->o_next;
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

static void handle_wait_task(void)
{
    KERNELOBJ *kobj = kobjs_list_head.o_next;

    while (kobj != &kobjs_list_tail) {
        TASKCTRLBLK *ptask = kobj->w_head;
        TASKCTRLBLK *pready;

        while (ptask != NULL) {
            pready = ptask;
            ptask  = ptask->t_next;

            /* 判断等待是否超时 */
            if (pready->t_timeout != -1 && --pready->t_timeout == 0) {
                /* 将 pready 从等待队列中删除 */
                if (pready->t_next) pready->t_next->t_prev = pready->t_prev;
                else kobj->w_tail = pready->t_prev;
                if (pready->t_prev) pready->t_prev->t_next = pready->t_next;
                else kobj->w_head = pready->t_next;

                /* 设置等待超时标记 */
                pready->o_type |= KOBJ_TASK_TIMEOUT;

                /* 将 pready 加入就绪队列 */
                ready_enqueue(pready);
            }
        }
        kobj = kobj->o_next;
    }
}

static void interrupt new_int_1ch(void)
{
    INTERRUPT_OFF();

    /* system tick counter */
    g_tick_counter++;

    /* 当前运行的任务放入就绪队列尾部 */
    ready_enqueue(g_running_task);
    g_prevtask = g_running_task;

    handle_sleep_task(); /* 处理休眠任务 */
    handle_wait_task (); /* 处理等待任务 */

    /* 取出就绪任务 */
    g_nexttask = ready_dequeue();

    /* 进行任务切换 */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;
    g_running_task = g_nexttask;
    g_idle_counter+=(g_running_task == (TASKCTRLBLK*)g_idletask);

    INTERRUPT_ON();
}

static void interrupt switch_task(void)
{
    if (g_prevtask) {
        g_prevtask->t_ss = _SS;
        g_prevtask->t_sp = _SP;
    }
    if (g_nexttask) {
        _SS = g_nexttask->t_ss;
        _SP = g_nexttask->t_sp;
        g_running_task = g_nexttask;
    }
    INTERRUPT_ON();
}

/* 任务运行结束的处理函数 */
static void far task_done_handler(TASKCTRLBLK far *ptask)
{
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
    g_nexttask = ready_dequeue();

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

    /* 初始化内核对象列表 */
    kobjs_list_head.o_next = &kobjs_list_tail;
    kobjs_list_head.o_prev = &kobjs_list_tail;
    kobjs_list_tail.o_next = &kobjs_list_head;
    kobjs_list_tail.o_prev = &kobjs_list_head;

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

    INTERRUPT_ON();
}

void ffkernel_exit(void)
{
    INTERRUPT_OFF();

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

    INTERRUPT_OFF();

    /* 任务控制块清零 */
    memset(ptask, 0, sizeof(TASKCTRLBLK));

    /* 指向任务栈顶 */
    stack = (int*)ptask + size / 2;

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
    *--stack =_ES;               /* es */
    *--stack =_DS;               /* ds */
    *--stack = 0;                /* si */
    *--stack = 0;                /* di */
    *--stack = 0;                /* bp */

    /* 保存任务堆栈入口 */
    ptask->t_ss = FP_SEG(stack);
    ptask->t_sp = FP_OFF(stack);

    ready_enqueue(ptask); /* 加入就绪队列 */
    kobjs_append((KERNELOBJ*)ptask); /* 加入内核对象列表 */

    INTERRUPT_ON();
    return 0;
}

int task_destroy(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    INTERRUPT_OFF();
    kobjs_remove((KERNELOBJ*)ptask); /* 移除 ptask */
    if (ptask->t_next) ptask->t_next->t_prev = ptask->t_prev;
    if (ptask->t_prev) ptask->t_prev->t_next = ptask->t_next;

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
    INTERRUPT_ON();

    return 0;
}

int task_suspend(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务已经运行结束 */
    if (ptask->o_type & KOBJ_TASK_DONE) return FFTASK_DONE;

    INTERRUPT_OFF();
    if (g_running_task == ptask) { /* 欲挂起的任务为当前运行任务 */
        g_prevtask = g_running_task;
        g_nexttask = ready_dequeue();
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
    if (ptask->o_type & KOBJ_TASK_DONE) return FFTASK_DONE;

    INTERRUPT_OFF();
    if (g_running_task != ptask) { /* 如果欲恢复的任务不在当前运行状态 */
        ready_enqueue(g_running_task);
        g_prevtask = g_running_task;
        g_nexttask = ptask;
        switch_task();
    } else {
        INTERRUPT_ON();
    }
    return 0;
}

/* 任务休眠 */
int task_sleep(int ms)
{
    /* ms 为零直接返回 */
    if (ms <= 0) return 0;

    INTERRUPT_OFF();

    /* 设置任务休眠时间 */
    g_running_task->t_timeout = (ms + 9) / 10;

    /* 将当前运行的任务放入休眠队列尾 */
    g_running_task->t_prev =  sleep_list_tail.t_prev;
    g_running_task->t_next = &sleep_list_tail;
    g_running_task->t_prev->t_next = g_running_task;
    g_running_task->t_next->t_prev = g_running_task;

    /* 从就绪队列取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = ready_dequeue();

    /* 进行任务切换 */
    switch_task();
    return 0;
}

int task_wait(void *ctask, int timeout)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;
    int          ret;

    if (ptask == NULL || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1; /* 参数有效性检查 */
    if (ptask->o_type & KOBJ_TASK_DONE) return 0; /* 如果任务已经运行结束 */

    timeout = (timeout == -1) ? -1 : (timeout + 9) / 10; /* 转换为 tick 为单位 */
    if (timeout == 0) return FFWAIT_TIMEOUT; /* 如果超时等于零 */

    INTERRUPT_OFF();

    /* 将当前任务放入 ptask 的等待队列尾 */
    wait_enqueue((KERNELOBJ*)ptask, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = ready_dequeue();

    /* 进行任务切换 */
    switch_task();

    /* 超时处理 */
    INTERRUPT_OFF();
    ret = (g_running_task->o_type & KOBJ_TASK_TIMEOUT) ? FFWAIT_TIMEOUT : 0;
    g_running_task->o_type &= ~KOBJ_TASK_TIMEOUT;
    INTERRUPT_ON();
    return ret;
}

int task_exitcode(void *ctask, int *code)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;

    /* 参数有效性检查 */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* 如果任务未运行结束 */
    if ((ptask->o_type & KOBJ_TASK_DONE) == 0) return FFTASK_NOTEXIT;

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
    kobjs_append(pmutex); /* 加入内核对象列表 */
    return 0;
}

/* 销毁互斥体 */
int mutex_destroy(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;
    kobjs_remove(pmutex); /* 从内核对象列表移除 */
    return 0;
}

int mutex_lock(void *cmutex, int timeout)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;
    int        ret;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    /* 转换为 tick 为单位 */
    timeout = (timeout == -1) ? -1 : (timeout + 9) / 10;

    INTERRUPT_OFF();

    /* 互斥体加锁 */
    if (pmutex->o_data0 > 0) {
        pmutex->o_data0--;
        pmutex->o_owner = g_running_task;
        INTERRUPT_ON();
        return 0;
    }

    /* 如果超时等于零 */
    if (timeout == 0) {
        INTERRUPT_ON();
        return FFWAIT_TIMEOUT;
    }

    /* 将当前任务放入 pmutex 的等待队列尾 */
    wait_enqueue(pmutex, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = ready_dequeue();

    /* 进行任务切换 */
    switch_task();

    /* 设置 mutex 的所有者和超时处理 */
    INTERRUPT_OFF();
    if (g_running_task->o_type & KOBJ_TASK_TIMEOUT) {
        g_running_task->o_type &= ~KOBJ_TASK_TIMEOUT;
        ret = FFWAIT_TIMEOUT;
    } else {
        pmutex->o_owner = g_running_task;
        ret =  0;
    }
    INTERRUPT_ON();
    return ret;
}

int mutex_unlock(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* 参数有效性检查 */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    INTERRUPT_OFF();

    /* 当前任务不是 mutex 的所有者 */
    if (pmutex->o_owner != g_running_task) {
        INTERRUPT_ON();
        return FFMUTEX_NOTOWNER;
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
    ready_enqueue(g_running_task);
    g_prevtask = g_running_task;

    /* 将第一个等待 pmutex 的任务取出 */
    g_nexttask = wait_dequeue(pmutex);

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
    kobjs_append(psem); /* 加入内核对象列表 */
    return 0;
}

int sem_destroy(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;
    kobjs_remove(psem); /* 从内核对象列表移除 */
    return 0;
}

int sem_wait(void *csem, int timeout)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;
    int        ret;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* 转换为 tick 为单位 */
    timeout = (timeout == -1) ? -1 : (timeout + 9) / 10;

    INTERRUPT_OFF();

    /* 如果信号量大于零 */
    if (psem->o_data0 > 0) {
        psem->o_data0--;
        INTERRUPT_ON();
        return 0;
    }

    /* 如果超时等于零 */
    if (timeout == 0) {
        INTERRUPT_ON();
        return FFWAIT_TIMEOUT;
    }

    /* 将当前任务放入 psem 的等待队列尾 */
    wait_enqueue(psem, g_running_task, timeout);

    /* 取出就绪任务 */
    g_prevtask = g_running_task;
    g_nexttask = ready_dequeue();

    /* 进行任务切换 */
    switch_task();

    /* 超时处理 */
    INTERRUPT_OFF();
    ret = (g_running_task->o_type & KOBJ_TASK_TIMEOUT) ? FFWAIT_TIMEOUT : 0;
    g_running_task->o_type &= ~KOBJ_TASK_TIMEOUT;
    INTERRUPT_ON();
    return ret;
}

int sem_post(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

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
    ready_enqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = wait_dequeue(psem); /* 将第一个等待 psem 的任务取出 */

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

int sem_post_interrupt(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* 参数有效性检查 */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* 如果没有任务等待该互斥体则返回 */
    if (!psem->w_head) {
        if (psem->o_data0 < psem->o_data1) {
            psem->o_data0++;
        }
        return 0;
    }

    /* 将当前运行任务放入就绪队列尾 */
    ready_enqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = wait_dequeue(psem); /* 将第一个等待 psem 的任务取出 */
    return 1;
}
