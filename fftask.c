/* ����ͷ�ļ� */
#include <stdlib.h>
#include <stdio.h>
#include <dos.h>
#include "fftask.h"

/* Ԥ���뿪�� */
#define FOR_REAL_DOS  0

/* �ú�������������δʹ�õľ��� */
#define DO_USE_VAR(var)  do { var = var; } while (0)

/* �ڲ��������� */
#define INTERRUPT_OFF()  do { disable(); } while (0)  /* ���ж� */
#define INTERRUPT_ON()   do { enable (); } while (0)  /* ���ж� */

/* �ڲ����Ͷ��� */
/* ������ƿ����Ͷ��� */
#define TASK_STACK_SIZE  1024  /* �����ջ��С */
typedef struct tagTASKCTRLBLK
{
    /* ����������� */
    struct tagTASKCTRLBLK *t_next;
    struct tagTASKCTRLBLK *t_prev;
    int t_retv;     /* ���񷵻�ֵ */
    int t_ss;       /* �����ջ ss */
    int t_sp;       /* �����ջ sp */
    int t_stack[0]; /* �����ջ */
} TASKCTRLBLK;      /* ������ƿ� */


/* �ڲ�ȫ�ֱ������� */
static void interrupt (*old_int_1ch)(void); /* ���ڱ���ɵ�ʱ���ж� */

static TASKCTRLBLK  maintask;       /* ������ */
static TASKCTRLBLK  ready_list_head;/* �����������ͷ */
static TASKCTRLBLK  ready_list_tail;/* �����������β */
static TASKCTRLBLK  wait_list_head; /* ����ȴ�����ͷ */
static TASKCTRLBLK  wait_list_tail; /* ����ȴ�����β */
static TASKCTRLBLK *g_running_task; /* ��ǰ���е����� */
static TASKCTRLBLK *g_prevtask;     /* �����л���ǰһ������ */
static TASKCTRLBLK *g_nexttask;     /* �����л�����һ������ */
static TASKCTRLBLK *g_idletask;     /* ָ�����������ƿ� */

unsigned long g_nSysTickCounter   = 0;  /* �ñ������ڼ�¼ϵͳ tick ���� */
unsigned long g_nTaskReadyCounter = 0;  /* �ñ������ڼ�¼����������� */
unsigned long g_nTaskIdleCounter  = 0;  /* �ñ������ڼ�¼����������� */

/* �ڲ�����ʵ�� */
/* ++ ������й����� ++ */
/* ����������� */
static void readyenqueue(TASKCTRLBLK *ptcb)
{
    if (ptcb != g_idletask) {
        ptcb->t_prev =  ready_list_tail.t_prev;
        ptcb->t_next = &ready_list_tail;
        ptcb->t_prev->t_next = ptcb;
        ptcb->t_next->t_prev = ptcb;
    }
}

/* �������г��� */
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

/* �ȴ�������� */
static void waitenqueue(TASKCTRLBLK *ptcb)
{
    ptcb->t_prev =  wait_list_tail.t_prev;
    ptcb->t_next = &wait_list_tail;
    ptcb->t_prev->t_next = ptcb;
    ptcb->t_next->t_prev = ptcb;
}
/* -- ������й����� -- */


static void interrupt new_int_1ch(void)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* system tick counter */
    g_nSysTickCounter++;

    /* ��ǰ���е���������������β�� */
    g_prevtask = g_running_task;
    readyenqueue(g_prevtask);

    /* ȡ���������� */
    g_nexttask = readydequeue();
    g_running_task = g_nexttask;

    /* ���������л� */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;

    /* ����ж����� */
    outp(0x20, 0x20);

    INTERRUPT_ON();
}

static void interrupt switch_task(void)
{
    INTERRUPT_OFF();

    /* ���������л� */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;

    INTERRUPT_ON();
}

/* �������н����Ĵ����� */
static void far task_done_handler(TASKCTRLBLK far *ptcb)
{
    /* �������񷵻�ֵ */
    ptcb->t_retv = _AX;

    /* ���ж� */
    INTERRUPT_OFF();

    /* �����������Ǹ��������н��� */
    ptcb->t_next = NULL;
    ptcb->t_prev = NULL;

    /* ���ж� */
    INTERRUPT_ON();
}

/* ϵͳ�������� */
static int far idle_task_proc(void far *p)
{
    union REGS regs = {0};

    /* �������뾯�� */
    DO_USE_VAR(p);

#if FOR_REAL_DOS
    while (1) { asm hlt; }
#else
    while (1) {
        /* �ͷŴ�������Դ */
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
    /* �ָ�ʱ���ж� */
    setvect(0x1c, old_int_1ch);
    old_int_1ch = NULL;
    outportb(0x43, 0x3c);
    outportb(0x40, 0x00);
    outportb(0x40, 0x00);
}

/* ����ʵ�� */
int ffkernel_init(void)
{
    /* ��ʼ��������� */
    /* �������� */
    ready_list_head.t_next = &ready_list_tail;
    ready_list_head.t_prev = &ready_list_tail;
    ready_list_tail.t_next = &ready_list_head;
    ready_list_tail.t_prev = &ready_list_head;

    /* �ȴ����� */
    wait_list_head .t_next = &wait_list_tail;
    wait_list_head .t_prev = &wait_list_tail;
    wait_list_tail .t_next = &wait_list_head;
    wait_list_tail .t_prev = &wait_list_head;

    /* ��ʼ������ָ�� */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* ������������ */
    g_idletask = task_create(idle_task_proc, NULL);

    /* ����������Ӿ���������ɾ�� */
    if (g_idletask) {
        g_idletask->t_next->t_prev = g_idletask->t_prev;
        g_idletask->t_prev->t_next = g_idletask->t_next;
    }

    /* ��ʼ�� int 1ch �ж� */
    setup_int1ch();

    return 0;
}

void ffkernel_exit(void)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* restore int 1ch */
    restore_int1ch();

    /* �رտ������� */
    if (g_idletask) {
        task_destroy(g_idletask);
    }

    /* ���ж� */
    INTERRUPT_ON();

    /* �л��������� */
    g_prevtask     = g_running_task;
    g_nexttask     = &maintask;
    g_running_task = g_nexttask;
    switch_task();
}

void* task_create(TASK task, void far *p)
{
    TASKCTRLBLK *ptcb  = NULL;
    int         *stack = NULL;

    /* ������Ч�Լ�� */
    if (!task) return 0;

    /* ����������ƿ� */
    ptcb = calloc(1, sizeof(TASKCTRLBLK) + TASK_STACK_SIZE);
    if (!ptcb) goto done;

    /* ָ������ջ�� */
    stack = (ptcb->t_stack + TASK_STACK_SIZE);

    /* ������ƿ��ַ��ջ */
    *--stack =  FP_SEG(ptcb);
    *--stack =  FP_OFF(ptcb);

    /* ������� p ��ջ */
    *--stack =  FP_SEG(p);
    *--stack =  FP_OFF(p);

    /* ���������������ַ��ջ */
    *--stack =  FP_SEG(task_done_handler);
    *--stack =  FP_OFF(task_done_handler);

    /* ��ʼ�������ջ */
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

    /* ���������ջ��� */
    ptcb->t_ss = FP_SEG(stack);
    ptcb->t_sp = FP_OFF(stack);

    /* ���ж� */
    INTERRUPT_OFF();

    /* ����������� */
    readyenqueue(ptcb);

    /* ���ж� */
    INTERRUPT_ON();

done:
    /* ���������� */
    return ptcb;
}

int task_destroy(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* ������Ч�Լ�� */
    if (!ptcb) return -1;

    /* �������� */
    task_suspend(htask);

    /* �ͷ�������ƿ� */
    if (htask) free(htask);
    return 0;
}

int task_suspend(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* ������Ч�Լ�� */
    if (!ptcb) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* �����������Ϊ��ǰ�������� */
    if (g_running_task == ptcb) {
        /* ȡ���������� */
        g_prevtask     = g_running_task;
        g_nexttask     = readydequeue();
        g_running_task = g_nexttask;

        /* ���������л� */
        switch_task();
    }
    else { /* �����������Ϊ��ǰ�������� */
        if (ptcb->t_next) ptcb->t_next->t_prev = ptcb->t_prev;
        if (ptcb->t_prev) ptcb->t_prev->t_next = ptcb->t_next;
    }

    /* ���ж� */
    INTERRUPT_ON();

    return 0;
}

int task_resume(void *htask)
{
    TASKCTRLBLK *ptcb = htask;

    /* ������Ч�Լ�� */
    if (!ptcb) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������ָ��������ڵ�ǰ����״̬ */
    if (g_running_task != ptcb) {
        /* ����ǰ������������������β */
        g_prevtask = g_running_task;
        readyenqueue(g_prevtask);

        /* ptch ��Ϊ��ǰ���� */
        g_nexttask     = ptcb;
        g_running_task = ptcb;

        /* ���������л� */
        switch_task();
    }

    /* ���ж� */
    INTERRUPT_ON();
    return 0;
}

int task_join   (void *htask) { return 0; }
int task_sleep  (int ms  ) { return 0; }
int task_exit   (int code) { return 0; }


