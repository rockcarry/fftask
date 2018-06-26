#ifndef __FFTASK_H__
#define __FFTASK_H__

/* 常量定义 */
#define FFTASK_SIZE  22
#define FFKOBJ_SIZE  16

#define FFTASK_DONE       -2
#define FFTASK_NOTEXIT    -3
#define FFMUTEX_NOTOWNER  -4
#define FFWAIT_TIMEOUT    -5

/* 任务函数类型定义 */
typedef int far (*TASK)(void far *p);

/* 全局变量声明 */
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

#endif



