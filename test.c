/* 包含头文件 */
#include <stdlib.h>
#include <stdio.h>
#include "fftask.h"

static void *g_dos_mutex = NULL;
static void *g_event     = NULL;

static int far task1(void far *p)
{
    int i = 100;
    while (i--) {
        *(int far*)p = i;
        mutex_lock(g_dos_mutex, -1);
        printf("task1 is running...\r\n");
        mutex_unlock(g_dos_mutex);
        task_sleep(100);
    }
    return 12345;
}

static int far task2(void far *p)
{
    int i = 100;
    while (i--) {
        *(int far*)p = i;
        mutex_lock(g_dos_mutex, -1);
        printf("task2 is running...\r\n");
        mutex_unlock(g_dos_mutex);
        task_sleep(200);
    }
    return 54321;
}

static int far task3(void far *p)
{
    while (1) {
        (*(int far*)p)++;
        mutex_lock(g_dos_mutex, -1);
        printf("task3 is running...\r\n");
        mutex_unlock(g_dos_mutex);
        task_sleep(500);
    }
}

static int far task4(void far *p)
{
    float         usage    = 0;
    unsigned long lasttick = 0;
    unsigned long difftick = 0;
    unsigned long lastidle = 0;
    unsigned long diffidle = 0;
    while (1) {
        difftick = g_tick_counter - lasttick;
        lasttick = g_tick_counter;
        diffidle = g_idle_counter - lastidle;
        lastidle = g_idle_counter;
        usage    = 100f * (difftick - diffidle) / difftick;
        *(float far *)p = usage;
        mutex_lock(g_dos_mutex, -1);
        printf("task4 is running...\r\n");
        mutex_unlock(g_dos_mutex);
        task_sleep(1000);
    }
}

void main(void)
{
    void *htask1    = NULL;
    void *htask2    = NULL;
    void *htask3    = NULL;
    void *htask4    = NULL;
    int   exitcode1 = 0;
    int   exitcode2 = 0;
    int   p1   = 1;
    int   p2   = 2;
    int   p3   = 3;
    float p4   = 0;
    int   stop = 0;

    /* 初始化多任务系统 */
    ffkernel_init();
    g_dos_mutex = mutex_create();

    /* 创建任务 */
    htask1 = task_create(task1, &p1, 0);
    htask2 = task_create(task2, &p2, 0);
    htask3 = task_create(task3, &p3, 0);
    htask4 = task_create(task4, &p4, 0);

    while (!stop) {
        mutex_lock(g_dos_mutex, -1);
        stop = kbhit();
        printf("p1 = %d, p2 = %d, p3 = %d, p4 = %.1f\r\n", p1, p2, p3, p4);
        printf("g_tick_counter = %ld, g_idle_counter = %ld\r\n", g_tick_counter, g_idle_counter);
        mutex_unlock(g_dos_mutex);
        if (!stop) task_sleep(1000);
    }

    mutex_lock(g_dos_mutex, -1);
    printf("please wait task1 and task2 done.\r\n");
    printf("please wait task1 and task2 done.\r\n");
    printf("please wait task1 and task2 done.\r\n");
    mutex_unlock(g_dos_mutex);

    task_wait(htask1, -1);
    task_exitcode(htask1, &exitcode1);
    mutex_lock(g_dos_mutex, -1);
    printf("wait htask1 done, exitcode = %u.\r\n", exitcode1);
    mutex_unlock(g_dos_mutex);

    task_wait(htask2, -1);
    task_exitcode(htask2, &exitcode2);
    mutex_lock(g_dos_mutex, -1);
    printf("wait htask2 done, exitcode = %u.\r\n", exitcode2);
    mutex_unlock(g_dos_mutex);

    task_destroy(htask1);
    task_destroy(htask2);
    task_destroy(htask3);
    task_destroy(htask4);

    mutex_destroy(g_dos_mutex);
    ffkernel_exit();

    getch();
    printf("done.\r\n");
    getch();
}

