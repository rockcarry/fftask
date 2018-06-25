#ifndef __FFTASK_H__
#define __FFTASK_H__

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

void* sem_create   (int initval, int maxval);
int   sem_destroy  (void *hsem);
int   sem_wait     (void *hsem, int timeout);
int   sem_post     (void *hsem);
int   sem_getval   (void *hsem, int *value);

/*
+------+
| 说明 |
+------+
 所有接口 int 型返回值，0 表示成功，-1 表示非法参数，-2 有特殊含义
 返回值如果为 -2，含义如下：
 task_suspend  - 任务已经运行结束
 task_resume   - 任务已经运行结束
 task_exitcode - 任务还没有结束运行
 mutex_unlock  - 当前任务不是 mutex 的所有者
 */

#endif



