#ifndef __FFTASK_H__
#define __FFTASK_H__

/* 常量定义 */
#define TASK_CTXT_SIZE  18
#define KOBJ_CTXT_SIZE  16

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
 task_wait     - 等待超时
 mutex_lock    - 等待超时
 sem_wait      - 等待超时
 */

#endif



