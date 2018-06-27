/* 包含头文件 */
#include <stdlib.h>
#include <string.h>
#include <mem.h>
#include <dos.h>
#include "fftask.h"
#include "wavdev.h"

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned long  DWORD;

/*
  DMA
 */
/* 内部常量定义 */
/* 以下是 PC 机的 DMA 控制器端口的常量定义 */
#define DMA8_CMD_PORT      0x08   /* 8 位 DMA 写命令寄存器端口 */
#define DMA8_STATU_PORT    0x08   /* 8 位 DMA 独状态寄存器端口 */
#define DMA8_REQUEST_PORT  0x09   /* 8 位 DMA 写请求寄存器端口 */
#define DMA8_MASK_PORT     0x0A   /* 8 位 DMA 屏蔽寄存器端口（只写）*/
#define DMA8_MODE_PORT     0x0B   /* 8 位 DMA 写模式寄存器端口 */
#define DMA8_CLRPTR_PORT   0x0C   /* 8 位 DMA 清先后状态寄存器端口 */
#define DMA8_RESET_PORT    0x0D   /* 8 位 DMA 写复位命令端口 */

#define DMA16_CMD_PORT     0xD0   /* 16 位 DMA 写命令寄存器端口 */
#define DMA16_STATU_PORT   0xD0   /* 16 位 DMA 独状态寄存器端口 */
#define DMA16_REQUEST_PORT 0xD2   /* 16 位 DMA 写请求寄存器端口 */
#define DMA16_MASK_PORT    0xD4   /* 16 位 DMA 屏蔽寄存器端口（只写）*/
#define DMA16_MODE_PORT    0xD6   /* 16 位 DMA 写模式寄存器端口 */
#define DMA16_CLRPTR_PORT  0xD8   /* 16 位 DMA 清先后状态寄存器端口 */
#define DMA16_RESET_PORT   0xDA   /* 16 位 DMA 写复位命令端口 */

/* 以下是 PC 机 DMA 的 7 个通道的地址寄存器、计数寄存器和页面寄存器的端口常量定义 */
/* 其中通道 0-3 用于 8 位的 DMA，通道 4-7 用于 16 位 DMA */
/* PC机中，规定通道 2 用于进行软盘的 DMA 传输，其余通道可供用户使用 */
#define DMA0_ADDR_PORT     0x00   /* 通道 0 的地址寄存器 */
#define DMA0_COUNT_PORT    0x01   /* 通道 0 的计数寄存器 */
#define DMA0_PAGE_PORT     0x87   /* 通道 0 的页面寄存器 */

#define DMA1_ADDR_PORT     0x02   /* 通道 1 的地址寄存器 */
#define DMA1_COUNT_PORT    0x03   /* 通道 1 的计数寄存器 */
#define DMA1_PAGE_PORT     0x83   /* 通道 1 的页面寄存器 */

#define DMA3_ADDR_PORT     0x06   /* 通道 3 的地址寄存器 */
#define DMA3_COUNT_PORT    0x07   /* 通道 3 的计数寄存器 */
#define DMA3_PAGE_PORT     0x82   /* 通道 3 的页面寄存器 */

#define DMA5_ADDR_PORT     0xC4   /* 通道 5 的地址寄存器 */
#define DMA5_COUNT_PORT    0xC6   /* 通道 5 的计数寄存器 */
#define DMA5_PAGE_PORT     0x8B   /* 通道 5 的页面寄存器 */

#define DMA6_ADDR_PORT     0xC8   /* 通道 6 的地址寄存器 */
#define DMA6_COUNT_PORT    0xCA   /* 通道 6 的计数寄存器 */
#define DMA6_PAGE_PORT     0x89   /* 通道 6 的页面寄存器 */

#define DMA7_ADDR_PORT     0xCC   /* 通道 7 的地址寄存器 */
#define DMA7_COUNT_PORT    0xCE   /* 通道 7 的计数寄存器 */
#define DMA7_PAGE_PORT     0x8A   /* 通道 7 的页面寄存器 */

/* 内部函数实现 */
static int initdma8bit(int channel, BYTE far *addr, int size)
{
    DWORD phyaddr = FP_SEG(addr) * 0x10L + FP_OFF(addr);
    BYTE  page    = (BYTE)(phyaddr >> 16);
    WORD  offset  = (WORD)(phyaddr >> 0 );

    if (channel > 3 || channel == 2) return -1;
    outportb(DMA8_MASK_PORT, channel | (1 << 2));  /* 屏蔽该通道 */
    outportb(DMA8_MODE_PORT, channel | (1 << 4) | (2 << 2)); /* 请求方式 + 自动初始化 + 读传送 */
    outportb(DMA8_CLRPTR_PORT, 0);
    size--;
    switch (channel) {
    case 0:
        outportb(DMA0_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA0_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA0_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA0_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA0_PAGE_PORT ,  page);
        break;
    case 1:
        outportb(DMA1_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA1_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA1_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA1_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA1_PAGE_PORT ,  page);
        break;
    case 3:
        outportb(DMA3_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA3_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA3_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA3_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA3_PAGE_PORT ,  page);
        break;
    }
    outportb(DMA8_MASK_PORT, channel);
    return 0;
}

static void closedma8bit(int channel)
{
    if (channel > 3 || channel == 2) return;
    outportb(DMA8_MASK_PORT, channel | (1 << 2));  /* 屏蔽该通道 */
}

#if 0  /* todo.. */
static int initdma16bit(int channel, BYTE far *addr, int size)
{
    DWORD phyaddr = FP_SEG(addr) * 0x10L + FP_OFF(addr);
    BYTE  page    = (BYTE)(phyaddr >> 17);
    WORD  offset  = (WORD)(phyaddr >> 1);

    if (channel < 5) return -1;
    outportb(DMA16_MASK_PORT, (channel - 4) | (1 << 2));  /* 屏蔽该通道 */
    outportb(DMA16_MODE_PORT, (channel - 4) | (1 << 4) | (2 << 2)); /* 请求方式 + 自动初始化 + 读传送 */
    outportb(DMA16_CLRPTR_PORT, 0);
    size /= 2; size--;
    switch (channel) {
    case 5:
        outportb(DMA5_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA5_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA5_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA5_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA5_PAGE_PORT ,  page);
        break;
    case 6:
        outportb(DMA6_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA6_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA6_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA6_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA6_PAGE_PORT ,  page);
        break;
    case 7:
        outportb(DMA7_COUNT_PORT, (BYTE)(size   >> 0));
        outportb(DMA7_COUNT_PORT, (BYTE)(size   >> 8));
        outportb(DMA7_ADDR_PORT , (BYTE)(offset >> 0));
        outportb(DMA7_ADDR_PORT , (BYTE)(offset >> 8));
        outportb(DMA7_PAGE_PORT ,  page);
        break;
    }
    outportb(DMA16_MASK_PORT, channel - 4);
    return 0;
}

static void closedma16bit(int channel)
{
    if (channel <= 4) return;
    outportb(DMA16_MASK_PORT, (channel - 4) | (1 << 2));  /* 屏蔽该通道 */
}
#endif


/*
  dsp
 */
/* 内部类型定义 */
typedef struct {
    char dspenvstr[128];
    WORD dspversion;
    WORD dspbaseioport;
    WORD resetport;
    WORD writedataport;
    WORD writestatusport;
    WORD readdataport;
    WORD readstatusport;
    WORD mixerbaseioport;
    WORD mpu401baseioport;
    BYTE dspirqnum;
    BYTE dspdma8;
    BYTE dspdma16;
} DSP;

/* 内部常量定义 */
#define DSP_RESET_DELAY   10
#define DSP_READY         0xAA

/* DSP 命令常量定义 */
#define DSP_GET_VERSION   0xE1
#define DSP_SET_BLK_SIZE  0x48
#define DSP_START_DMA8    0x1C
#define DSP_PAUSE_DMA8    0xD0
#define DSP_SET_SAMPRATE  0x40

/* 内部函数实现 */
static int parse_sb_envstr(char *envstr, char id)
{
    char buf[32] = "0x";
    int  i, j;
    for (i = 0; envstr[i] != id && envstr[i] != '\0' && i < 128; i++);
    if (envstr[i] == '\0' || i == 128) return 0;
    else i++;
    for (j = 2; j < 32 && envstr[i] != ' '; j++) buf[j] = envstr[i++];
    return (int)strtoul(buf, NULL, 0);
}

static int readdsp(DSP *dsp)
{
    while (!(inportb(dsp->readstatusport) & (1 << 7)));
    return(inportb(dsp->readdataport));
}

static void writedsp(DSP *dsp, BYTE byte)
{
    while (inportb(dsp->writestatusport) & (1 << 7));
    outportb(dsp->writedataport, byte);
}

static void resetdsp(DSP *dsp)
{
    int i;
    outportb(dsp->resetport, 1);
    for (i=0; i<DSP_RESET_DELAY; i++) inportb(dsp->resetport);
    outportb(dsp->resetport,0);
    while (readdsp(dsp) != DSP_READY);
}

static int initdsp(DSP *dsp)
{
    if (!getenv("BLASTER")) return -1;
    strupr(strcpy(dsp->dspenvstr, getenv("BLASTER")));

    dsp->dspbaseioport    = parse_sb_envstr(dsp->dspenvstr, 'A');
    dsp->resetport        = dsp->dspbaseioport + 0x06;
    dsp->writedataport    = dsp->dspbaseioport + 0x0C;
    dsp->writestatusport  = dsp->dspbaseioport + 0x0C;
    dsp->readdataport     = dsp->dspbaseioport + 0x0A;
    dsp->readstatusport   = dsp->dspbaseioport + 0x0E;
    dsp->dspirqnum        = parse_sb_envstr(dsp->dspenvstr, 'I');
    dsp->dspdma8          = parse_sb_envstr(dsp->dspenvstr, 'D');
    dsp->dspdma16         = parse_sb_envstr(dsp->dspenvstr, 'H');
    dsp->mixerbaseioport  = parse_sb_envstr(dsp->dspenvstr, 'M');
    dsp->mpu401baseioport = parse_sb_envstr(dsp->dspenvstr, 'P');

    /* reset dsp */
    resetdsp(dsp);

    /* get version */
    writedsp(dsp, DSP_GET_VERSION);
    dsp->dspversion  = readdsp(dsp) << 8;
    dsp->dspversion |= readdsp(dsp);
    return 0;
}

static void closedsp(DSP *dsp)
{
    /* reset dsp */
    resetdsp(dsp);
}

#ifdef DEBUG
static void printdspinfo(DSP *dsp)
{
    printf("DSP_ENVSTR:           %s\r\n", dsp->dspenvstr);
    printf("DSP_VERSION:          %x\r\n", dsp->dspversion);
    printf("DSP_BASEIOPORT:       %x\r\n", dsp->dspbaseioport);
    printf("DSP_RESETPORT:        %x\r\n", dsp->resetport);
    printf("DSP_WRITEDATAPORT:    %x\r\n", dsp->writedataport);
    printf("DSP_WRITESTATUSPORT:  %x\r\n", dsp->writestatusport);
    printf("DSP_READDATAPORT:     %x\r\n", dsp->readdataport);
    printf("DSP_READSTATUSPORT:   %x\r\n", dsp->readstatusport);
    printf("DSP_MIXERBASEIOPORT:  %x\r\n", dsp->mixerbaseioport);
    printf("DSP_MPU401BASEIOPORT: %x\r\n", dsp->mpu401baseioport);
    printf("DSP_IRQNUM:           %x\r\n", dsp->dspirqnum);
    printf("DSP_DMA8:             %x\r\n", dsp->dspdma8);
    printf("DSP_DMA16:            %x\r\n", dsp->dspdma16);
}
#endif

static void dspsetsamplerate(DSP *dsp, WORD rate)
{
    WORD timeconst = (WORD)(65536L - (256000000L / rate));
    writedsp(dsp, DSP_SET_SAMPRATE);
    writedsp(dsp, timeconst >> 8);
}

static void dspsetblocksize(DSP *dsp, WORD blksize)
{
    blksize--;
    writedsp(dsp, DSP_SET_BLK_SIZE);
    writedsp(dsp, (BYTE)(blksize >> 0));
    writedsp(dsp, (BYTE)(blksize >> 8));
}

static void dspintdone(DSP *dsp)
{
    inportb(dsp->readstatusport);
}

static void dspstartdma8bit(DSP *dsp)
{
    writedsp(dsp, DSP_START_DMA8);
}

static void dsppausedma8bit(DSP *dsp)
{
    writedsp(dsp, DSP_PAUSE_DMA8);
}


/*
  wavdev
 */
/* 内部常量定义 */
#define WAVDEV_BUFSIZE   2000

/* 内部全局变量定义 */
static DSP  g_dsp_dev  = {0};
static int  g_dma_flag =  0;
static char g_dma_buf[WAVDEV_BUFSIZE * 2 + 1];
static char g_irq_sem[FFKOBJ_SIZE];
static void interrupt (*old_dsp_int_handler)(void) = NULL;

/* 内部函数实现 */
static void interrupt new_dsp_int_handler(void)
{
    disable();
    dspintdone(&g_dsp_dev);
    outportb(0x20, 0x20);
    if (sem_post_interrupt(g_irq_sem) == 1) {
        INT_TASK_SWITCH();
    }
    enable();
}

/* 函数实现 */
void wavdev_init(int samprate)
{
    /* create irq sem */
    sem_create(g_irq_sem, 2, 2);

    /* init dsp */
    initdsp(&g_dsp_dev);
    dspsetsamplerate(&g_dsp_dev, samprate);
    dspsetblocksize (&g_dsp_dev, WAVDEV_BUFSIZE);

    /* init dma */
    initdma8bit(g_dsp_dev.dspdma8, g_dma_buf, WAVDEV_BUFSIZE * 2);

    /* unmask dsp irq */
    outportb(0x21, inportb(0x21) & ~(1 << g_dsp_dev.dspirqnum));

    /* install irq handler */
    old_dsp_int_handler = getvect(g_dsp_dev.dspirqnum + 8);
    setvect(g_dsp_dev.dspirqnum + 8, new_dsp_int_handler);
}

void wavdev_exit(void)
{
    /* force stop */
    wavdev_stop();

    /* restore irq handler */
    setvect(g_dsp_dev.dspirqnum + 8, old_dsp_int_handler);

    /* mask dsp irq */
    outportb(0x21, inportb(0x21) | (1 << g_dsp_dev.dspirqnum));

    /* close dma */
    closedma8bit(g_dsp_dev.dspdma8);

    /* close dsp */
    closedsp(&g_dsp_dev);

    /* destroy irq sem*/
    sem_destroy(g_irq_sem);
}

void wavdev_start(void)
{
    dspstartdma8bit(&g_dsp_dev);
}

void wavdev_stop(void)
{
    dsppausedma8bit(&g_dsp_dev);
}

int wavdev_write(char *buf, int size)
{
    int ret = 0;
    while (size > 0 && ret == 0) {
        ret = sem_wait(g_irq_sem, -1);
        if (ret == 0) {
            char far *p = g_dma_buf + g_dma_flag++ * WAVDEV_BUFSIZE;
            int n = WAVDEV_BUFSIZE;
            if (n > size) n = size;
            do { *p++ = *buf++; size--; } while (n--);
            g_dma_flag %= 2;
        }
    }
    return ret;
}

#ifdef _TEST_
#include <stdio.h>
static char play_task_ctxt[1024];
static int far play_task_proc(void far *p)
{
    static char buf[WAVDEV_BUFSIZE];
    FILE *fp = fopen((char*)p, "rb");
    if (fp) {
        while (!feof(fp)) {
            int ret = fread(buf, 1, WAVDEV_BUFSIZE, fp);
            if (ret > 0) {
                if (ret < WAVDEV_BUFSIZE) {
                    memset(buf + ret, 0, WAVDEV_BUFSIZE - ret);
                }
                wavdev_write(buf, WAVDEV_BUFSIZE);
            }
        }
        fclose(fp);
    }
    return 0;
}

void main(void)
{
    ffkernel_init();
    wavdev_init(16000);
    wavdev_start();

    task_create(play_task_proc, "test.wav", play_task_ctxt, sizeof(play_task_ctxt));
    task_wait(play_task_ctxt, -1);

    wavdev_stop();
    wavdev_exit();
    ffkernel_exit();
}
#endif




