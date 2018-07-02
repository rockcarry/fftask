#ifndef __FFTASK_H__
#define __FFTASK_H__

/* 常量定义 */
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
} TASKCTRLBLK;     /* 任务控制块 */

/* 内核对象类型定义 */
typedef struct tagKERNELOBJ {
    KOBJ_COMMON_MEMBERS
    int   o_data0;
    int   o_data1;
    void *o_owner;
} KERNELOBJ;

#define FFTASK_SIZE  (sizeof(TASKCTRLBLK))
#define FFKOBJ_SIZE  (sizeof(KERNELOBJ  ))

/* 任务函数类型定义 */
typedef int far (*TASK)(void far *p);

/* 全局变量声明 */
extern TASKCTRLBLK *g_running_task;   /* 当前运行的任务 */
extern TASKCTRLBLK *g_prevtask;       /* 任务切换的前一个任务 */
extern TASKCTRLBLK *g_nexttask;       /* 任务切换的下一个任务 */
extern unsigned long g_tick_counter;  /* 该变量用于记录系统 tick 次数 */
extern unsigned long g_idle_counter;  /* 该变量用于记录空闲任务次数 */

/* 函数声明 */
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

/*++ 中断服务中 post 信号量 ++*/
int sem_post_interrupt(void *csem);
#define INT_TASK_SWITCH() do {  \
    g_prevtask->t_ss = _SS;     \
    g_prevtask->t_sp = _SP;     \
    _SS = g_nexttask->t_ss;     \
    _SP = g_nexttask->t_sp;     \
    g_running_task = g_nexttask;\
} while (0)
/*-- 中断服务中 post 信号量 --*/

#endif



