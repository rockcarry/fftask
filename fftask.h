#ifndef __FFTASK_H__
#define __FFTASK_H__

/* �������� */
#define FFTASK_DONE       -2
#define FFTASK_NOTEXIT    -3
#define FFMUTEX_NOTOWNER  -4
#define FFWAIT_TIMEOUT    -5

#define KOBJ_COMMON_MEMBERS \
    struct tagKERNELOBJ   *o_next; \
    struct tagKERNELOBJ   *o_prev; \
    struct tagTASKCTRLBLK *w_head; \
    struct tagTASKCTRLBLK *w_tail; \
    int    o_type;

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
} TASKCTRLBLK;     /* ������ƿ� */

/* �ں˶������Ͷ��� */
typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
    int   o_data0;
    int   o_data1;
    void *o_owner;
} KERNELOBJ;

#define FFTASK_SIZE  (sizeof(TASKCTRLBLK))
#define FFKOBJ_SIZE  (sizeof(KERNELOBJ  ))

/* ���������Ͷ��� */
typedef int far (*TASK)(void far *p);

/* ȫ�ֱ������� */
extern TASKCTRLBLK *g_running_task;   /* ��ǰ���е����� */
extern TASKCTRLBLK *g_prevtask;       /* �����л���ǰһ������ */
extern TASKCTRLBLK *g_nexttask;       /* �����л�����һ������ */
extern unsigned long g_tick_counter;  /* �ñ������ڼ�¼ϵͳ tick ���� */
extern unsigned long g_idle_counter;  /* �ñ������ڼ�¼����������� */

/* �������� */
void ffkernel_init(void);
void ffkernel_exit(void);

int  task_create  (TASK taskfunc, void far *taskparam, void *ctask, int size);
int  task_destroy (void *ctask);
int  task_suspend (void *ctask);
int  task_resume  (void *ctask);
int  task_sleep   (int ms);
int  task_delay   (int ms);
int  task_wait    (void *ctask, int timeout);
int  task_exitcode(void *ctask, int *code);

int  mutex_create (void *cmutex);
int  mutex_destroy(void *cmutex);
int  mutex_lock   (void *cmutex, int timeout);
int  mutex_unlock (void *cmutex);

int  sem_create   (void *csem, int initval, int maxval);
int  sem_destroy  (void *csem);
int  sem_wait     (void *csem, int timeout);
int  sem_post     (void *csem);
int  sem_getval   (void *csem, int *value);

/*++ �жϷ����� post �ź��� ++*/
int sem_post_interrupt(void *csem);
#define INT_TASK_SWITCH() do {  \
    g_prevtask->t_ss = _SS;     \
    g_prevtask->t_sp = _SP;     \
    _SS = g_nexttask->t_ss;     \
    _SP = g_nexttask->t_sp;     \
    g_running_task = g_nexttask;\
} while (0)
/*-- �жϷ����� post �ź��� --*/

#endif



