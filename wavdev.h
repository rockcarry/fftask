#ifndef __WAVDEV_H__
#define __WAVDEV_H__

/* 常量定义 */
#define WAVDEV_BUFSIZE 2000

/* 函数声明 */
void wavdev_init (int channels, unsigned samprate, int sampsize);
void wavdev_exit (void);
void wavdev_play (int play);
int  wavdev_write(char *buf, int size);

#endif



