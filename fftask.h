#ifndef __FFTASK_H__
#define __FFTASK_H__

/* 常量定义 */
#define FEVENT_INITSET   (1 << 3)
#define FEVENT_AUTORESET (1 << 4)
#define FEVENT_WAKESALL  (1 << 5)

/* 任务函数类型定义 */
typedef int far (*TASK)(void far *p);

/* 全局变量声明 */
extern unsigned long g_tick_counter ;  /* 该变量用于记录系统 tick 次数 */
extern unsigned long g_ready_counter;  /* 该变量用于记录就绪任务次数 */
extern unsigned long g_idle_counter ;  /* 该变量用于记录空闲任务次数 */

/* 函数声明 */
int   ffkernel_init(void);
void  ffkernel_exit(void);

void* task_create  (TASK task, void far *p, int size);
int   task_destroy (void *htask);
int   task_suspend (void *htask);
int   task_resume  (void *htask);
int   task_sleep   (int ms);
int   task_wait    (void *htask, int timeout);
int   task_exitcode(void *htask, int *code);

void* mutex_create (void);
int   mutex_destroy(void *hmutex);
int   mutex_lock   (void *hmutex, int timeout);
int   mutex_unlock (void *hmutex);

void* event_create (int flags);
int   event_destroy(void *hevent);
int   event_wait   (void *hevent, int  timeout);
int   event_setval (void *hevent, int  value);
int   event_getval (void *hevent, int *value);

void* sem_create   (int value);
int   sem_destroy  (void *hsem);
int   sem_wait     (void *hsem, int timeout);
int   sem_post     (void *hsem);

#endif



