#ifndef __WAVDEV_H__
#define __WAVDEV_H__

/* �������� */
#define WAVDEV_BUFSIZE 2000

/* �������� */
void wavdev_init (int samprate);
void wavdev_exit (void);
void wavdev_start(void);
void wavdev_stop (void);
int  wavdev_write(char *buf, int size);

#endif



