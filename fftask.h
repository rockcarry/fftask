#ifndef __FFTASK_H__
#define __FFTASK_H__

/* 任务函数类型定义 */
typedef int far (*TASK)(void far *p);

int  ffkernel_init(void);
void ffkernel_exit(void);

void* task_create (TASK task, void far *p);
int   task_destroy(void *htask);
int   task_suspend(void *htask);
int   task_resume (void *htask);
int   task_join   (void *htask);
int   task_sleep  (int  ms  );
int   task_exit   (int  code);

#endif

/*
todo..
mutex_create
mutex_destroy
mutex_lock
mutex_unlock

sem_create
sem_destroy
sem_wait
sem_post
*/


