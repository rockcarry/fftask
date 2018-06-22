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

/* Ĭ�������ջ��С */
#define TASK_STACK_SIZE  1024

/* �ڲ����Ͷ��� */
/* ������ƿ����Ͷ��� */
typedef struct tagTASKCTRLBLK {
    KOBJ_COMMON_MEMBERS
    int t_timeout; /* �������߳�ʱ */
    int t_retv;    /* ���񷵻�ֵ */
    int t_ss;      /* �����ջ ss */
    int t_sp;      /* �����ջ sp */
    int t_stack[0];/* �����ջ */
} TASKCTRLBLK;     /* ������ƿ� */

typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
} KERNELOBJ;

/* �ڲ�ȫ�ֱ������� */
static void interrupt (*old_int_1ch)(void); /* ���ڱ���ɵ�ʱ���ж� */

static TASKCTRLBLK  maintask        = {0};  /* ������ */
static TASKCTRLBLK  ready_list_head = {0};  /* �����������ͷ */
static TASKCTRLBLK  ready_list_tail = {0};  /* �����������β */
static TASKCTRLBLK  sleep_list_head = {0};  /* �������߶���ͷ */
static TASKCTRLBLK  sleep_list_tail = {0};  /* �������߶���β */
static TASKCTRLBLK *g_running_task  = NULL; /* ��ǰ���е����� */
static TASKCTRLBLK *g_prevtask      = NULL; /* �����л���ǰһ������ */
static TASKCTRLBLK *g_nexttask      = NULL; /* �����л�����һ������ */
static TASKCTRLBLK *g_idletask      = NULL; /* ָ�����������ƿ� */

unsigned long g_tick_counter = 1;  /* �ñ������ڼ�¼ϵͳ tick ���� */
unsigned long g_idle_counter = 1;  /* �ñ������ڼ�¼����������� */

/* �ڲ�����ʵ�� */
/* ++ ������й����� ++ */
/* ����������� */
static void readyenqueue(TASKCTRLBLK *ptask)
{
    if (ptask == g_idletask) return;
    ptask->o_prev =  ready_list_tail.o_prev;
    ptask->o_next = &ready_list_tail;
    ptask->o_prev->o_next = ptask;
    ptask->o_next->o_prev = ptask;
}

/* �������г��� */
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

/* ���߶������ */
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
/* -- ������й����� -- */

/* �������߶��� */
static void handle_sleep_task(void)
{
    TASKCTRLBLK *ptask = sleep_list_tail.o_prev;
    TASKCTRLBLK *pready;

    /* ��β��ͷ�������߶��� */
    while (ptask != &sleep_list_head) {
        pready = ptask;
        ptask  = ptask->o_prev;

        /* �ж������Ƿ���� */
        if (--pready->t_timeout == 0) {
            /* �� pready �����߶�����ɾ�� */
            pready->o_next->o_prev = pready->o_prev;
            pready->o_prev->o_next = pready->o_next;

            /* �� pready �����������ͷ */
            pready->o_next =  ready_list_head.o_next;
            pready->o_prev = &ready_list_head;
            pready->o_prev->o_next = pready;
            pready->o_next->o_prev = pready;
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
    g_prevtask = g_running_task;
    readyenqueue(g_running_task);

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
    g_idle_counter+=(g_running_task == g_idletask);

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
        makeallwaitready((KERNELOBJ*)ptask);
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
int ffkernel_init(void)
{
    int ret = -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ��ʼ��������� */
    /* �������� */
    ready_list_head.o_next = &ready_list_tail;
    ready_list_head.o_prev = &ready_list_tail;
    ready_list_tail.o_next = &ready_list_head;
    ready_list_tail.o_prev = &ready_list_head;

    /* ���߶��� */
    sleep_list_head.o_next = &sleep_list_tail;
    sleep_list_head.o_prev = &sleep_list_tail;
    sleep_list_tail.o_next = &sleep_list_head;
    sleep_list_tail.o_prev = &sleep_list_head;

    /* ��ʼ������ָ�� */
    g_running_task = &maintask;
    g_prevtask     = &maintask;
    g_nexttask     = &maintask;

    /* ������������ */
    g_idletask = task_create(idle_task_proc, NULL, 256);
    if (!g_idletask) goto done;

    /* ����������Ӿ���������ɾ�� */
    g_idletask->o_next->o_prev = g_idletask->o_prev;
    g_idletask->o_prev->o_next = g_idletask->o_next;

    /* ��ʼ�� int 1ch �ж� */
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
    /* ���ж� */
    INTERRUPT_ON();
    return ret;
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

void* task_create(TASK task, void far *p, int size)
{
    TASKCTRLBLK *ptask = NULL;
    int         *stack = NULL;

    /* ������Ч�Լ�� */
    if (!task) return 0;
    if (!size) size = TASK_STACK_SIZE;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ����������ƿ� */
    ptask = malloc(sizeof(TASKCTRLBLK) + size);
    if (!ptask) goto done;

    /* ������ƿ����� */
    memset(ptask, 0, sizeof(TASKCTRLBLK));

    /* ָ������ջ�� */
    stack = (ptask->t_stack + size);

    /* ������ƿ��ַ��ջ */
    *--stack = FP_SEG(ptask);
    *--stack = FP_OFF(ptask);

    /* ������� p ��ջ */
    *--stack = FP_SEG(p);
    *--stack = FP_OFF(p);

    /* ���������������ַ��ջ */
    *--stack = FP_SEG(task_done_handler);
    *--stack = FP_OFF(task_done_handler);

    /* ��ʼ�������ջ */
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

    /* ���������ջ��� */
    ptask->t_ss = FP_SEG(stack);
    ptask->t_sp = FP_OFF(stack);

    /* ����������� */
    readyenqueue(ptask);

done:
    /* ���ж� */
    INTERRUPT_ON();

    /* ���������� */
    return ptask;
}

int task_destroy(void *htask)
{
    TASKCTRLBLK *ptask = htask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������������Ϊ��ǰ�������� */
    if (g_running_task == ptask) {
        free(htask);
        g_prevtask = NULL;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* �����������Ϊ��ǰ�������� */
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

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ��������Ѿ����н��� */
    if (ptask->o_type & KOBJ_TASK_DONE) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* �����������Ϊ��ǰ�������� */
    if (g_running_task == ptask) {
        /* ȡ���������� */
        g_prevtask = g_running_task;
        g_nexttask = readydequeue();
        switch_task();
    } else { /* �����������Ϊ��ǰ�������� */
        if (ptask->o_next) ptask->o_next->o_prev = ptask->o_prev;
        if (ptask->o_prev) ptask->o_prev->o_next = ptask->o_next;
        INTERRUPT_ON();
    }

    return 0;
}

int task_resume(void *htask)
{
    TASKCTRLBLK *ptask = htask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* ��������Ѿ����н��� */
    if (ptask->o_type & KOBJ_TASK_DONE) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ������ָ��������ڵ�ǰ����״̬ */
    if (g_running_task != ptask) {
        /* ����ǰ������������������β */
        g_prevtask = g_running_task;
        readyenqueue(g_prevtask);

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

int task_wait(void *htask, int timeout)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)htask;

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

int task_exitcode(void *htask, int *code)
{
    TASKCTRLBLK *ptask = (TASKCTRLBLK*)htask;

    /* ������Ч�Լ�� */
    if (!ptask || (ptask->o_type & KOBJ_TYPE_MASK) != KOBJ_TYPE_TASK) return -1;

    /* �������δ���н��� */
    if ((ptask->o_type & KOBJ_TASK_DONE) == 0) return -1;

    /* ���ؽ����� */
    if (code) *code = ptask->t_retv;

    return 0;
}

/* ���������� */
void* mutex_create(void)
{
    KERNELOBJ *pmutex;

    /* Ϊ��������仺���� */
    pmutex = calloc(1, sizeof(KERNELOBJ));
    if (!pmutex) return 0;

    /* ��ʼ�������� */
    pmutex->o_type = KOBJ_TYPE_MUTEX;
    return pmutex;
}

/* ���ٻ����� */
int mutex_destroy(void *hmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* �ͷŻ������������ */
    if (pmutex) free(pmutex);
    return 0;
}

int mutex_lock(void *hmutex, int timeout)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ��������� */
    if (pmutex->o_data++ == 0) {
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
    return 0;
}

int mutex_unlock(void *hmutex)
{
    KERNELOBJ *pmutex = (KERNELOBJ*)hmutex;

    /* ������Ч�Լ�� */
    if (!pmutex || pmutex->o_type != KOBJ_TYPE_MUTEX) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    /* ��������� */
    if (pmutex->o_data > 0) pmutex->o_data--;

    /* ���û������ȴ��û������򷵻� */
    if (!pmutex->w_head) {
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������������������β */
    g_prevtask = g_running_task;
    readyenqueue(g_running_task);

    /* ����һ���ȴ� pmutex ������ȡ�� */
    g_nexttask = waitdequeue(pmutex);

    /* ���������л� */
    switch_task();
    return 0;
}

void* event_create(int flags)
{
    KERNELOBJ *pevent;

    /* Ϊ event ���仺���� */
    pevent = calloc(1, sizeof(KERNELOBJ));
    if (!pevent) return 0;

    /* ��ʼ�� event */
    pevent->o_type = KOBJ_TYPE_EVENT | (flags & 0xf8);
    return pevent;
}

int event_destroy(void *hevent)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* ������Ч�Լ�� */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* �ͷŻ������������ */
    if (pevent) free(pevent);
}

int event_wait(void *hevent, int timeout)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* ������Ч�Լ�� */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* ���ж� */
    INTERRUPT_OFF();
    if (pevent->o_type & KOBJ_EVENT_VALUE) {
        if (pevent->o_type & KOBJ_EVENT_AUTORESET) {
            pevent->o_type &= ~KOBJ_EVENT_VALUE;
        }
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������� pevent �ĵȴ�����β */
    waitenqueue(pevent, g_running_task, timeout);

    /* ȡ���������� */
    g_prevtask = g_running_task;
    g_nexttask = readydequeue();

    /* ���������л� */
    switch_task();

    if (pevent->o_type & KOBJ_EVENT_AUTORESET) {
        pevent->o_type &= ~KOBJ_EVENT_VALUE;
    }
}

int event_setval(void *hevent, int value)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* ������Ч�Լ�� */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* ���ж� */
    INTERRUPT_OFF();

    pevent->o_type &= ~KOBJ_EVENT_VALUE;
    pevent->o_type |=  value ? KOBJ_EVENT_VALUE : 0;

    /* ���û������ȴ��û������򷵻� */
    if (!value || !pevent->w_head) {
        INTERRUPT_ON();
        return 0;
    }

    /* ����ǰ������������������β */
    readyenqueue(g_running_task);
    g_prevtask = g_running_task;

    if (pevent->o_type & KOBJ_EVENT_WAKESALL) {
        /* �����еȴ� pevent ����������������ͷ */
        makeallwaitready(pevent);
        g_nexttask = readydequeue(); /* �ٴӾ�������ȡ������ */
    } else {
        /* ����һ���ȴ� pevent ������ȡ�� */
        g_nexttask = waitdequeue(pevent);
    }

    /* ���������л� */
    switch_task();
    return 0;
}

int event_getval(void *hevent, int *value)
{
    KERNELOBJ *pevent = (KERNELOBJ*)hevent;

    /* ������Ч�Լ�� */
    if (!pevent || pevent->o_type != KOBJ_TYPE_EVENT) return -1;

    /* ���� event ֵ */
    *value = (pevent->o_type & KOBJ_EVENT_VALUE) ? 1 : 0;
    return 0;
}

