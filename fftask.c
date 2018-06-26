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

/* Ĭ�������ջ��С */
#define TASK_STACK_SIZE  1024

/* �ڲ����Ͷ��� */
/* ������ƿ����Ͷ��� */
typedef struct tagTASKCTRLBLK {
    KOBJ_COMMON_MEMBERS
    struct tagTASKCTRLBLK *t_next;
    struct tagTASKCTRLBLK *t_prev;
    int t_timeout; /* �������߳�ʱ */
    int t_retv;    /* ���񷵻�ֵ */
    int t_ss;      /* �����ջ ss */
    int t_sp;      /* �����ջ sp */
    int t_stack[0];/* �����ջ */
} TASKCTRLBLK;     /* ������ƿ� */

typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
    int   o_data0;
    int   o_data1;
    void *o_owner;
} KERNELOBJ;

/* �ڲ�ȫ�ֱ������� */
static void interrupt (*old_int_1ch)(void); /* ���ڱ���ɵ�ʱ���ж� */

static TASKCTRLBLK  maintask        = {0};  /* ������ */
static TASKCTRLBLK  ready_list_head = {0};  /* �����������ͷ */
static TASKCTRLBLK  ready_list_tail = {0};  /* �����������β */
static TASKCTRLBLK  sleep_list_head = {0};  /* �������߶���ͷ */
static TASKCTRLBLK  sleep_list_tail = {0};  /* �������߶���β */
static KERNELOBJ    kobj_list_head  = {0};  /* �ں˶����б�ͷ */
static KERNELOBJ    kobj_list_tail  = {0};  /* �ں˶����б�β */
static TASKCTRLBLK *g_running_task  = NULL; /* ��ǰ���е����� */
static TASKCTRLBLK *g_prevtask      = NULL; /* �����л���ǰһ������ */
static TASKCTRLBLK *g_nexttask      = NULL; /* �����л�����һ������ */
static char         g_idletask[256];        /* ����������ƿ� */

unsigned long g_tick_counter = 1;  /* �ñ������ڼ�¼ϵͳ tick ���� */
unsigned long g_idle_counter = 1;  /* �ñ������ڼ�¼����������� */

/* �ڲ�����ʵ�� */
/* ++ ������й����� ++ */
/* ����������� */
static void readyenqueue(TASKCTRLBLK *ptask)
{
    if (ptask == (TASKCTRLBLK*)g_idletask) return;
    ptask->t_prev =  ready_list_tail.t_prev;
    ptask->t_next = &ready_list_tail;
    ptask->t_prev->t_next = ptask;
    ptask->t_next->t_prev = ptask;
}

/* �������г��� */
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

/* ���߶������ */
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
/* -- ������й����� -- */

/* �������߶��� */
static void handle_sleep_task(void)
{
    TASKCTRLBLK *ptask = sleep_list_tail.t_prev;
    TASKCTRLBLK *pready;

    /* ��β��ͷ�������߶��� */
    while (ptask != &sleep_list_head) {
        pready = ptask;
        ptask  = ptask->t_prev;

        /* �ж������Ƿ���� */
        if (--pready->t_timeout == 0) {
            /* �� pready �����߶�����ɾ�� */
            pready->t_next->t_prev = pready->t_prev;
            pready->t_prev->t_next = pready->t_next;

            /* �� pready �����������ͷ */
            pready->t_next =  ready_list_head.t_next;
            pready->t_prev = &ready_list_head;
            pready->t_prev->t_next = pready;
            pready->t_next->t_prev = pready;
        }
    }
}

static void interrupt new_int_1ch(void)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* system tick counter */
    g_tick_counter++;

    /* ��ǰ���е���������������β�� */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

    /* �������߶��� */
    handle_sleep_task();

    /* ȡ���������� */
    g_nexttask = readydequeue();

    /* ���������л� */
    g_prevtask->t_ss = _SS;
    g_prevtask->t_sp = _SP;
    _SS = g_nexttask->t_ss;
    _SP = g_nexttask->t_sp;
    g_running_task = g_nexttask;
    g_idle_counter+=(g_running_task == (TASKCTRLBLK*)g_idletask);

    /* ���ж� */
    INTERRUPT_ON();

    /* ����ж����� */
    outp(0x20, 0x20);
}

static void interrupt switch_task(void)
{
    /* ���������л� */
    if (g_prevtask) {
        g_prevtask->t_ss = _SS;
        g_prevtask->t_sp = _SP;
    }
    if (g_nexttask) {
        _SS = g_nexttask->t_ss;
        _SP = g_nexttask->t_sp;
        g_running_task = g_nexttask;
    }
    /* ���ж� */
    INTERRUPT_ON();
}

/* �������н����Ĵ����� */
static void far task_done_handler(TASKCTRLBLK far *ptask)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* �������񷵻�ֵ */
    ptask->t_retv = _AX;

    /* ��Ǹ��������н��� */
    ptask->o_type |= KOBJ_TASK_DONE;

    /* �����еȴ� ptask ����������������ͷ */
    if (ptask->w_head) {
        ptask->w_tail->t_next =  ready_list_head.t_next;
        ready_list_head.t_next->t_prev = ptask->w_tail;
        ready_list_head.t_next = ptask->w_head;
        ptask->w_head->t_prev = &ready_list_head;
        ptask->w_head = ptask->w_tail = NULL;
    }

    /* ȡ���������� */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();
}

/* ϵͳ�������� */
static int far idle_task_proc(void far *p)
{
    /* �������뾯�� */
    DO_USE_VAR(p);
    while (1) {
        /* �ͷŴ�������Դ */
#if FOR_REAL_DOS
        asm hlt;
#else
        asm mov ax, 0x1680;
        asm int 0x2f;
#endif
    }
    /* return 0; */
}

/* ����ʵ�� */
void ffkernel_init(void)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* ��ʼ���������� */
    ready_list_head.t_next = &ready_list_tail;
    ready_list_head.t_prev = &ready_list_tail;
    ready_list_tail.t_next = &ready_list_head;
    ready_list_tail.t_prev = &ready_list_head;

    /* ��ʼ�����߶��� */
    sleep_list_head.t_next = &sleep_list_tail;
    sleep_list_head.t_prev = &sleep_list_tail;
    sleep_list_tail.t_next = &sleep_list_head;
    sleep_list_tail.t_prev = &sleep_list_head;

    /* ��ʼ������ָ�� */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* ������������ */
    task_create(idle_task_proc, NULL, g_idletask, sizeof(g_idletask));

    /* ����������Ӿ���������ɾ�� */
    ((TASKCTRLBLK*)g_idletask)->t_next->t_prev = ((TASKCTRLBLK*)g_idletask)->t_prev;
    ((TASKCTRLBLK*)g_idletask)->t_prev->t_next = ((TASKCTRLBLK*)g_idletask)->t_next;

    /* ��ʼ�� int 1ch �ж� */
    old_int_1ch = getvect(0x1c);
    setvect(0x1c, new_int_1ch);

    /* set to 100Hz freq */
    #define _8253_FREQ     1193181L
    #define _8253_COUNTER  11932L
    outportb(0x43, 0x3c);
    outportb(0x40, (_8253_COUNTER >> 0) & 0xff);
    outportb(0x40, (_8253_COUNTER >> 8) & 0xff);

    /* ���ж� */
    INTERRUPT_ON();
}

void ffkernel_exit(void)
{
    /* ���ж� */
    INTERRUPT_OFF();

    /* restore int 1ch */
    /* �ָ�ʱ���ж� */
    setvect (0x1c, old_int_1ch);
    outportb(0x43, 0x3c);
    outportb(0x40, 0x00);
    outportb(0x40, 0x00);

    /* �رտ������� */
    task_destroy(g_idletask);

    /* �л��������� */
    g_prevtask = g_running_task;
    g_nexttask = &maintask;

    /* ���������л� */
    switch_task();
}

int task_create(TASK taskfunc, void far *taskparam, void *ctask, int size)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;
    int         *stack = NULL;

    /* ������Ч�Լ�� */
    if (!ctask || size < 256) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������ƿ����� */
    memset(ptask, 0, sizeof(TASKCTRLBLK));

    /* ָ������ջ�� */
    stack = (ptask->t_stack + size - sizeof(TASKCTRLBLK));

    /* ������ƿ��ַ��ջ */
    *--stack = FP_SEG(ptask);
    *--stack = FP_OFF(ptask);

    /* ������� p ��ջ */
    *--stack = FP_SEG(taskparam);
    *--stack = FP_OFF(taskparam);

    /* ���������������ַ��ջ */
    *--stack = FP_SEG(task_done_handler);
    *--stack = FP_OFF(task_done_handler);

    /* ��ʼ�������ջ */
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

    /* ���������ջ��� */
    ptask->t_ss = FP_SEG(stack);
    ptask->t_sp = FP_OFF(stack);

    /* ����������� */
    readyenqueue(ptask);

    /* ���ж� */
    INTERRUPT_ON();
    return 0;
}

int task_destroy(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    if (g_running_task == ptask) { /* ����������Ϊ��ǰ�������� */
        g_prevtask = NULL;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* ����������Ϊ��ǰ�������� */
        if (ptask->t_next) ptask->t_next->t_prev = ptask->t_prev;
        if (ptask->t_prev) ptask->t_prev->t_next = ptask->t_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_suspend(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ��������Ѿ����н��� */
    if (ptask->o_type & KOBJ_TASK_DONE) return -2;

    /* ���ж� */
    INTERRUPT_OFF();

    /* �����������Ϊ��ǰ�������� */
    if (g_running_task == ptask) {
        /* ȡ���������� */
        g_prevtask = g_running_task;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* �����������Ϊ��ǰ�������� */
        if (ptask->t_next) ptask->t_next->t_prev = ptask->t_prev;
        if (ptask->t_prev) ptask->t_prev->t_next = ptask->t_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_resume(void *ctask)
{
    TASKCTRLBLK *ptask = ctask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ��������Ѿ����н��� */
    if (ptask->o_type & KOBJ_TASK_DONE) return -2;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������ָ��������ڵ�ǰ����״̬ */
    if (g_running_task != ptask) {
        /* ����ǰ������������������β */
        readyenqueue(g_running_task);
        g_prevtask = g_running_task;

        /* ptch ��Ϊ��ǰ���� */
        g_nexttask = ptask;

        /* ���������л� */
        switch_task();
    } else {
        /* ���ж� */
        INTERRUPT_ON();
    }
    return 0;
}

/* �������� */
int task_sleep(int ms)
{
    /* ms Ϊ��ֱ�ӷ��� */
    if (ms <= 0) return 0;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������������ʱ�� */
    g_running_task->t_timeout = (ms + 9) / 10;

    /* ����ǰ���е�����������߶���β */
    sleepenqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();
    return 0;
}

int task_wait(void *ctask, int timeout)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ��������Ѿ����н��� */
    if (ptask->o_type & KOBJ_TASK_DONE) return 0;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ����ǰ������� ptask �ĵȴ�����β */
    waitenqueue((KERNELOBJ*)ptask, g_running_task, timeout);

    /* ȡ���������� */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();
    return 0;
}

int task_exitcode(void *ctask, int *code)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)ctask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* �������δ���н��� */
    if ((ptask->o_type & KOBJ_TASK_DONE) == 0) return -2;

    /* ���ؽ����� */
    if (code) *code = ptask->t_retv;
    return 0;
}

/* ���������� */
int mutex_create(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;
    if (!cmutex) return -1;

    /* ��ʼ�������� */
    pmutex->o_type  = KOBJ_TYPE_MUTEX;
    pmutex->o_data0 = 1;
    return 0;
}

/* ���ٻ����� */
int mutex_destroy(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    return 0;
}

int mutex_lock(void *cmutex, int timeout)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ��������� */
    if (pmutex->o_data0 > 0) {
        pmutex->o_data0--;
        pmutex->o_owner = g_running_task;
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������� pmutex �ĵȴ�����β */
    waitenqueue(pmutex, g_running_task, timeout);

    /* ȡ���������� */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();

    INTERRUPT_OFF();
    pmutex->o_owner = g_running_task;
    INTERRUPT_ON();
    return 0;
}

int mutex_unlock(void *cmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)cmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || (pmutex->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_MUTEX) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ��ǰ������ mutex �������� */
    if (pmutex->o_owner != g_running_task) {
        INTERRUPT_ON();
        return -2;
    }

    /* ���û������ȴ��û������򷵻� */
    if (!pmutex->w_head) {
        if (pmutex->o_data0 < 1) {
            pmutex->o_data0++;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������������������β */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

    /* ����һ���ȴ� pmutex ������ȡ�� */
    g_nexttask = waitdequeue(pmutex);

    /* ���������л� */
    switch_task();
    return 0;
}

int sem_create(void *csem, int initval, int maxval)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;
    if (!csem) return -1;

    /* ��ʼ�� event */
    psem->o_type  = KOBJ_TYPE_SEM;
    psem->o_data0 = initval;
    psem->o_data1 = maxval ;
    return 0;
}

int sem_destroy(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* ������Ч�Լ�� */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    return 0;
}

int sem_wait(void *csem, int timeout)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* ������Ч�Լ�� */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ����ź��������� */
    if (psem->o_data0 > 0) {
        psem->o_data0--;
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������� psem �ĵȴ�����β */
    waitenqueue(psem, g_running_task, timeout);

    /* ȡ���������� */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();
}

int sem_post(void *csem)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* ������Ч�Լ�� */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ���û������ȴ��û������򷵻� */
    if (!psem->w_head) {
        if (psem->o_data0 < psem->o_data1) {
            psem->o_data0++;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������������������β */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;
    g_nexttask = waitdequeue(psem); /* ����һ���ȴ� psem ������ȡ�� */

    /* ���������л� */
    switch_task();
    return 0;
}

int sem_getval(void *csem, int *value)
{
    KERNELOBJ *psem = (KERNELOBJ*)csem;

    /* ������Ч�Լ�� */
    if (!psem || (psem->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_SEM) return -1;

    /* ���� sem ֵ */
    *value = psem->o_data1;
    return 0;
}

