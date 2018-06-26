#ifndef __FFTASK_H__
#define __FFTASK_H__

/* �������� */
#define TASK_CTXT_SIZE  18
#define KOBJ_CTXT_SIZE  16

/* ���������Ͷ��� */
typedef int far (*TASK)(void far *p);

/* ȫ�ֱ������� */
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
| ˵�� |
+------+
 ���нӿ� int �ͷ���ֵ��0 ��ʾ�ɹ���-1 ��ʾ�Ƿ�������-2 �����⺬��
 ����ֵ���Ϊ -2���������£�
 task_suspend  - �����Ѿ����н���
 task_resume   - �����Ѿ����н���
 task_exitcode - ����û�н�������
 mutex_unlock  - ��ǰ������ mutex ��������
 task_wait     - �ȴ���ʱ
 mutex_lock    - �ȴ���ʱ
 sem_wait      - �ȴ���ʱ
 */

#endif



