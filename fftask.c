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

/* 内部类型定义 */
/* 任务控制块类型定义 */
#define TASK_STACK_SIZE  1024  /* 任务堆栈大小 */
typedef struct tagTASKCTRLBLK
{
    /* 任务对象数据 */
    struct tagTASKCTRLBLK *t_next;
    struct tagTASKCTRLBLK *t_prev;
    int t_retv;     /* 任务返回值 */
    int t_ss;       /* 任务堆栈 ss */
    int t_sp;       /* 任务堆栈 sp */
    int t_stack[0]; /* 任务堆栈 */
} TASKCTRLBLK;      /* 任务控制块 */


/* 内部全局变量定义 */
static void interrupt (*old_int_1ch)(void); /* 用于保存旧的时钟中断 */

static TASKCTRLBLK  maintask;       /* 主任务 */
static TASKCTRLBLK  ready_list_head;/* 任务就绪队列头 */
static TASKCTRLBLK  ready_list_tail;/* 任务就绪队列尾 */
static TASKCTRLBLK  wait_list_head; /* 任务等待队列头 */
static TASKCTRLBLK  wait_list_tail; /* 任务等待队列尾 */
static TASKCTRLBLK *g_running_task; /* 当前运行的任务 */
static TASKCTRLBLK *g_prevtask;     /* 任务切换的前一个任务 */
static TASKCTRLBLK *g_nexttask;     /* 任务切换的下一个任务 */
static TASKCTRLBLK *g_idletask;     /* 指向空闲任务控制块 */

unsigned long g_nSysTickCounter   = 0;  /* 该变量用于记录系统 tick 次数 */
unsigned long g_nTaskReadyCounter = 0;  /* 该变量用于记录就绪任务次数 */
unsigned long g_nTaskIdleCounter  = 0;  /* 该变量用于记录空闲任务次数 */

/* 内部函数实现 */
/* ++ 任务队列管理函数 ++ */
/* 就绪队列入队 */
static void readyenqueue(TASKCTRLBLK *ptcb)
{
    if (ptcb != g_idletask) {
        ptcb->t_prev =  ready_list_tail.t_prev;
        ptcb->t_next = &ready_list_tail;
        ptcb->t_prev->t_next = ptcb;
        ptcb->t_next->t_prev = ptcb;
    }
}

/* 就绪队列出队 */
static TASKCTRLBLK* readydequeue(void)
{
    TASKCTRLBLK *ptcb;
    ptcb = ready_list_head.t_next;
    if (ptcb != &ready_list_tail) {
        g_nTaskReadyCounter++;
        ptcb->t_prev->t_next = ptcb->t_next;
        ptcb->t_next->t_prev = ptcb->t_prev;
        return ptcb;
    } else {
        g_nTaskIdleCounter ++;
        return g_idletask;
    }
}

/* 等待队列入队 */
static void waitenqueue(TASKCTRLBLK *ptcb)
{
    ptcb->t_prev =  wait_list_tail.t_prev;
    ptcb->t_next = &wait_list_tail;
    ptcb->t_prev->t_next = ptcb;
    ptcb->t_next->t_prev = ptcb;
}
/* -- 任务队列管理函数 -- */


static void interrupt new_int_1ch(void)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* system tick counter */
    g_nSysTickCounter++;

    /* 当前运行的任务放入就绪队列尾部 */
    g_prevtask = g_running_task;
    readyenqueue(g_prevtask);

    /* 取出就绪任务 */
    g_nexttask = readydequeue();
    g_running_task = g_nexttask;

    /* 进行任务切换 */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;

    /* 清除中断屏蔽 */
    outp(0x20, 0x20);

    INTERRUPT_ON();
}

static void interrupt switch_task(void)
{
    INTERRUPT_OFF();

    /* 进行任务切换 */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;

    INTERRUPT_ON();
}

/* 任务运行结束的处理函数 */
static void far task_done_handler(TASKCTRLBLK far *ptcb)
{
    /* 保存任务返回值 */
    ptcb->t_retv = _AX;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 用这个方法标记该任务运行结束 */
    ptcb->t_next = NULL;
    ptcb->t_prev = NULL;

    /* 开中断 */
    INTERRUPT_ON();
}

/* 系统空闲任务 */
static int far idle_task_proc(void far *p)
{
    union REGS regs = {0};

    /* 消除编译警告 */
    DO_USE_VAR(p);

#if FOR_REAL_DOS
    while (1) { asm hlt; }
#else
    while (1) {
        /* 释放处理器资源 */
        regs.x.ax = 0x1680;
        int86(0x2F, &regs, &regs);
    }
#endif

    /* return 0; */
}

static void setup_int1ch(void)
{
    old_int_1ch = getvect(0x1c);
    setvect(0x1c, new_int_1ch);

    /*++ set to 100Hz freq */
    #define _8253_FREQ     1193181L
    #define _8253_COUNTER  11932L
    outportb(0x43, 0x3c);
    outportb(0x40, (_8253_COUNTER >> 0) & 0xff);
    outportb(0x40, (_8253_COUNTER >> 8) & 0xff);
    /*-- set to 100Hz freq */
}

static void restore_int1ch(void)
{
    /* 恢复时钟中断 */
    setvect(0x1c, old_int_1ch);
    old_int_1ch = NULL;
    outportb(0x43, 0x3c);
    outportb(0x40, 0x00);
    outportb(0x40, 0x00);
}

/* 函数实现 */
int ffkernel_init(void)
{
    /* 初始化任务队列 */
    /* 就绪队列 */
    ready_list_head.t_next = &ready_list_tail;
    ready_list_head.t_prev = &ready_list_tail;
    ready_list_tail.t_next = &ready_list_head;
    ready_list_tail.t_prev = &ready_list_head;

    /* 等待队列 */
    wait_list_head .t_next = &wait_list_tail;
    wait_list_head .t_prev = &wait_list_tail;
    wait_list_tail .t_next = &wait_list_head;
    wait_list_tail .t_prev = &wait_list_head;

    /* 初始化任务指针 */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* 创建空闲任务 */
    g_idletask = task_create(idle_task_proc, NULL);

    /* 将空闲任务从就绪队列中删除 */
    if (g_idletask) {
        g_idletask->t_next->t_prev = g_idletask->t_prev;
        g_idletask->t_prev->t_next = g_idletask->t_next;
    }

    /* 初始化 int 1ch 中断 */
    setup_int1ch();

    return 0;
}

void ffkernel_exit(void)
{
    /* 关中断 */
    INTERRUPT_OFF();

    /* restore int 1ch */
    restore_int1ch();

    /* 关闭空闲任务 */
    if (g_idletask) {
        task_destroy(g_idletask);
    }

    /* 开中断 */
    INTERRUPT_ON();

    /* 切换到主任务 */
    g_prevtask     = g_running_task;
    g_nexttask     = &maintask;
    g_running_task = g_nexttask;
    switch_task();
}

void* task_create(TASK task, void far *p)
{
    TASKCTRLBLK *ptcb  = NULL;
    int         *stack = NULL;

    /* 参数有效性检查 */
    if (!task) return 0;

    /* 分配任务控制块 */
    ptcb = calloc(1, sizeof(TASKCTRLBLK) + TASK_STACK_SIZE);
    if (!ptcb) goto done;

    /* 指向任务栈顶 */
    stack = (ptcb->t_stack + TASK_STACK_SIZE);

    /* 任务控制块地址入栈 */
    *--stack =  FP_SEG(ptcb);
    *--stack =  FP_OFF(ptcb);

    /* 任务参数 p 入栈 */
    *--stack =  FP_SEG(p);
    *--stack =  FP_OFF(p);

    /* 任务结束处理函数地址入栈 */
    *--stack =  FP_SEG(task_done_handler);
    *--stack =  FP_OFF(task_done_handler);

    /* 初始化任务堆栈 */
    *--stack =  0x0200;       /* flag */
    *--stack =  FP_SEG(task); /* cs */
    *--stack =  FP_OFF(task); /* ip */
    *--stack =  0;            /* ax */
    *--stack =  0;            /* bx */
    *--stack =  0;            /* cx */
    *--stack =  0;            /* dx */
    *--stack =  0;            /* es */
    *--stack = _DS;           /* ds */
    *--stack =  0;            /* si */
    *--stack =  0;            /* di */
    *--stack =  0;            /* bp */

    /* 保存任务堆栈入口 */
    ptcb->t_ss = FP_SEG(stack);
    ptcb->t_sp = FP_OFF(stack);

    /* 关中断 */
    INTERRUPT_OFF();

    /* 加入就绪队列 */
    readyenqueue(ptcb);

    /* 开中断 */
    INTERRUPT_ON();

done:
    /* 返回任务句柄 */
    return ptcb;
}

int task_destroy(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* 参数有效性检查 */
    if (!ptcb) return -1;

    /* 挂起任务 */
    task_suspend(htask);

    /* 释放任务控制块 */
    if (htask) free(htask);
    return 0;
}

int task_suspend(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* 参数有效性检查 */
    if (!ptcb) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 欲挂起的任务为当前运行任务 */
    if (g_running_task == ptcb) {
        /* 取出就绪任务 */
        g_prevtask     = g_running_task;
        g_nexttask     = readydequeue();
        g_running_task = g_nexttask;

        /* 进行任务切换 */
        switch_task();
    }
    else { /* 欲挂起的任务不为当前运行任务 */
        if (ptcb->t_next) ptcb->t_next->t_prev = ptcb->t_prev;
        if (ptcb->t_prev) ptcb->t_prev->t_next = ptcb->t_next;
    }

    /* 开中断 */
    INTERRUPT_ON();

    return 0;
}

int task_resume(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* 参数有效性检查 */
    if (!ptcb) return -1;

    /* 关中断 */
    INTERRUPT_OFF();

    /* 如果欲恢复的任务不在当前运行状态 */
    if (g_running_task != ptcb) {
        /* 将当前运行任务放入就绪队列尾 */
        g_prevtask = g_running_task;
        readyenqueue(g_prevtask);

        /* ptch 作为当前任务 */
        g_nexttask     = ptcb;
        g_running_task = ptcb;

        /* 进行任务切换 */
        switch_task();
    }

    /* 开中断 */
    INTERRUPT_ON();
    return 0;
}

int task_join   (void *htask) { return 0; }
int task_sleep  (int ms  ) { return 0; }
int task_exit   (int code) { return 0; }


