#ifndef __FFTASK_H__
#define __FFTASK_H__

/* ���������Ͷ��� */
typedef int far (*TASK)(void far *p);

/* ȫ�ֱ������� */
extern unsigned long g_tick_counter ;  /* �ñ������ڼ�¼ϵͳ tick ���� */
extern unsigned long g_ready_counter;  /* �ñ������ڼ�¼����������� */
extern unsigned long g_idle_counter ;  /* �ñ������ڼ�¼����������� */

/* �������� */
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
| ˵�� |
+------+
 ���нӿ� int �ͷ���ֵ��0 ��ʾ�ɹ���-1 ��ʾ�Ƿ�������-2 �����⺬��
 ����ֵ���Ϊ -2���������£�
 task_suspend  - �����Ѿ����н���
 task_resume   - �����Ѿ����н���
 task_exitcode - ����û�н�������
 mutex_unlock  - ��ǰ������ mutex ��������
 */

#endif



