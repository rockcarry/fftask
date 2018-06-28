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
static BYTE DMAC_CTL_PORT_TAB[][7] = {
    /* cmd, req,  mask, mode, clr, rst  */
    { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D },
    { 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA },
};

/* 以下是 PC 机 DMA 的 7 个通道的地址寄存器、计数寄存器和页面寄存器的端口定义 */
/* 其中通道 0-3 用于 8 位的 DMA，通道 4-7 用于 16 位 DMA */
static BYTE DMAC_ACP_PORT_TAB[][3] = {
   /* addr, counter, page */
    { 0x00, 0x01, 0x87 },
    { 0x02, 0x03, 0x83 },
    { 0x04, 0x05, 0x81 },
    { 0x06, 0x07, 0x82 },
    { 0xC0, 0xC2, 0x8F },
    { 0xC4, 0xC6, 0x8B },
    { 0xC8, 0xCA, 0x89 },
    { 0xCC, 0xCE, 0x8A },
};

/* 内部函数实现 */
static int dmac_8237_init(int ch, int dma16, char far *addr, int size)
{
    DWORD phyaddr = (FP_SEG(addr) * 0x10L + FP_OFF(addr)) >> dma16;
    BYTE  page    = (BYTE)(phyaddr >> 16);
    WORD  offset  = (WORD)(phyaddr >> 0 );
    size -= 1;
    outportb(DMAC_CTL_PORT_TAB[dma16][2], (ch % 4) | (1 << 2)); /* 屏蔽该通道 */
    outportb(DMAC_CTL_PORT_TAB[dma16][3], (ch % 4) | (1 << 4) | (2 << 2)); /* 请求方式 + 自动初始化 + 读传送 */
    outportb(DMAC_CTL_PORT_TAB[dma16][4], 0);
    outportb(DMAC_ACP_PORT_TAB[ch][0], (BYTE)(offset >> 0));
    outportb(DMAC_ACP_PORT_TAB[ch][0], (BYTE)(offset >> 8));
    outportb(DMAC_ACP_PORT_TAB[ch][1], (BYTE)(size   >> 0));
    outportb(DMAC_ACP_PORT_TAB[ch][1], (BYTE)(size   >> 8));
    outportb(DMAC_ACP_PORT_TAB[ch][2],  page);
    outportb(DMAC_CTL_PORT_TAB[dma16][2], (ch % 4)); /* 解除屏蔽 */
}

static void dmac_8237_close(int ch, int dma16)
{
    outportb(DMAC_CTL_PORT_TAB[dma16][2], (ch % 4) | (1 << 2));  /* 屏蔽该通道 */
}

/*
  dsp
 */
/* 内部类型定义 */
typedef struct {
    char envstr[128];
    WORD version;
    WORD port_base;
    WORD port_reset;
    WORD port_read;
    WORD port_write;
    WORD port_status;
    WORD port_ack16;
    WORD port_mixer;
    WORD port_mpu401;
    BYTE irq_num;
    BYTE ch_dma8;
    BYTE ch_dma16;
} DSP;

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
    while (!(inportb(dsp->port_status) & (1 << 7)));
    return(inportb(dsp->port_read));
}

static void writedsp(DSP *dsp, BYTE byte)
{
    while (inportb(dsp->port_write) & (1 << 7));
    outportb(dsp->port_write, byte);
}

static void resetdsp(DSP *dsp)
{
    int i;
    outportb(dsp->port_reset, 1);
    for (i=0; i<10; i++) inportb(dsp->port_reset);
    outportb(dsp->port_reset,0);
    while (readdsp(dsp) != 0xAA);
}

static int dsp_init(DSP *dsp)
{
    if (!getenv("BLASTER")) return -1;
    strupr(strcpy(dsp->envstr, getenv("BLASTER")));

    dsp->port_base   = parse_sb_envstr(dsp->envstr, 'A');
    dsp->port_reset  = dsp->port_base + 0x06;
    dsp->port_read   = dsp->port_base + 0x0A;
    dsp->port_write  = dsp->port_base + 0x0C;
    dsp->port_status = dsp->port_base + 0x0E;
    dsp->port_ack16  = dsp->port_base + 0x0F;
    dsp->irq_num     = parse_sb_envstr(dsp->envstr, 'I');
    dsp->ch_dma8     = parse_sb_envstr(dsp->envstr, 'D');
    dsp->ch_dma16    = parse_sb_envstr(dsp->envstr, 'H');
    dsp->port_mixer  = parse_sb_envstr(dsp->envstr, 'M');
    dsp->port_mpu401 = parse_sb_envstr(dsp->envstr, 'P');

    /* reset dsp */
    resetdsp(dsp);

    /* get version */
    writedsp(dsp, 0xE1);
    dsp->version  = readdsp(dsp) << 8;
    dsp->version |= readdsp(dsp);
    return 0;
}

static void dsp_close(DSP *dsp)
{
    /* reset dsp */
    resetdsp(dsp);
}

#if 0
static void printdspinfo(DSP *dsp)
{
    printf("envstr:      %s\r\n", dsp->envstr     );
    printf("version:     %x\r\n", dsp->version    );
    printf("port_base:   %x\r\n", dsp->port_base  );
    printf("port_reset:  %x\r\n", dsp->port_reset );
    printf("port_read:   %x\r\n", dsp->port_read  );
    printf("port_write:  %x\r\n", dsp->port_write );
    printf("port_status: %x\r\n", dsp->port_status);
    printf("port_ack16:  %x\r\n", dsp->port_ack16 );
    printf("port_mixer:  %x\r\n", dsp->port_mixer );
    printf("port_mpu401: %x\r\n", dsp->port_mpu401);
    printf("irq_num:     %x\r\n", dsp->irq_num    );
    printf("ch_dma8:     %x\r\n", dsp->ch_dma8    );
    printf("ch_dma16:    %x\r\n", dsp->ch_dma16   );
}
#endif

static void dsp_int_done(DSP *dsp, int dma16)
{
    inportb(dma16 ? dsp->port_ack16 : dsp->port_status);
}

#define MODE_STEREO  (1 << 5)
#define MODE_SIGNED  (1 << 4)
static void dsp_dma_init(DSP *dsp, WORD blksize, WORD samprate, int dma16, int mode)
{
    blksize -= 1;
    if (dsp->version <= 0x201) {
        /* set sample rate */
        WORD val = (WORD)(65536L - (256000000L / samprate));
        writedsp(dsp, 0x40);
        writedsp(dsp, val >> 8);

        writedsp(dsp, 0x48);
        writedsp(dsp, (BYTE)(blksize >> 0));
        writedsp(dsp, (BYTE)(blksize >> 8));
        writedsp(dsp, 0x1C);
    } else {
        /* set sample rate */
        writedsp(dsp, 0x41);
        writedsp(dsp, (BYTE)(samprate >> 8));
        writedsp(dsp, (BYTE)(samprate >> 0));

        writedsp(dsp, dma16 ? 0xB6 : 0xC6);
        writedsp(dsp, (BYTE)mode);
        writedsp(dsp, (BYTE)(blksize >> 0));
        writedsp(dsp, (BYTE)(blksize >> 8));
    }
}

static void dsp_dma_close(DSP *dsp, int dma16)
{
    writedsp(dsp, dma16 ? 0xD9 : 0xDA);
}

static void dsp_dma_pause(DSP *dsp, int dma16, int pause)
{
    if (dsp->version <= 0x201) {
        writedsp(dsp, pause ? 0xD0 : 0x1C);
    } else {
        if (pause) {
            writedsp(dsp, dma16 ? 0xD5 : 0xD0);
        } else {
            writedsp(dsp, dma16 ? 0xD6 : 0xD4);
        }
    }
}


/*
  wavdev
 */
/* 内部全局变量定义 */
static char g_dma_buf[WAVDEV_BUFSIZE * 2 + 2];
static WORD g_dma_srate =  0;
static int  g_dma_16bit =  0;
static int  g_dma_mode  =  0;
static int  g_dma_flag  =  0;
static char g_irq_sem[FFKOBJ_SIZE];
static DSP  g_dsp_dev   = {0};
static void interrupt (*old_dsp_int_handler)(void) = NULL;

/* 内部函数实现 */
static void interrupt new_dsp_int_handler(void)
{
    dsp_int_done(&g_dsp_dev, g_dma_16bit);
    outportb(0x20, 0x20);
    disable();
    if (sem_post_interrupt(g_irq_sem) == 1) {
        INT_TASK_SWITCH();
    }
    enable();
}

/* 函数实现 */
void wavdev_init(int channels, unsigned samprate, int sampsize)
{
    /* create irq sem */
    sem_create(g_irq_sem, 2, 2);

    /* init dsp */
    dsp_init(&g_dsp_dev);
    if (g_dsp_dev.version <= 0x201) {
        channels = 1;
        sampsize = 8;
    }
    g_dma_srate = samprate;
    g_dma_16bit = sampsize == 16;
    g_dma_mode  = channels == 2  ? MODE_STEREO : 0;
    g_dma_mode |= sampsize == 16 ? MODE_SIGNED : 0;

    /* init 8237 dmac */
    memset(g_dma_buf, g_dma_16bit ? 0 : 0x80, sizeof(g_dma_buf));
    dmac_8237_init(g_dma_16bit ? g_dsp_dev.ch_dma16 : g_dsp_dev.ch_dma8, g_dma_16bit, g_dma_buf, WAVDEV_BUFSIZE * 2 >> g_dma_16bit);

    /* unmask dsp irq */
    outportb(0x21, inportb(0x21) & ~(1 << g_dsp_dev.irq_num));

    /* install irq handler */
    old_dsp_int_handler = getvect(g_dsp_dev.irq_num + 8);
    setvect(g_dsp_dev.irq_num + 8, new_dsp_int_handler);

    dsp_dma_init (&g_dsp_dev, WAVDEV_BUFSIZE >> g_dma_16bit, g_dma_srate, g_dma_16bit, g_dma_mode);
    dsp_dma_pause(&g_dsp_dev, g_dma_16bit, 1);
}

void wavdev_exit(void)
{
    /* restore irq handler */
    setvect(g_dsp_dev.irq_num + 8, old_dsp_int_handler);

    /* mask dsp irq */
    outportb(0x21, inportb(0x21) | (1 << g_dsp_dev.irq_num));

    /* close dma */
    dmac_8237_close(g_dma_16bit ? g_dsp_dev.ch_dma16 : g_dsp_dev.ch_dma8, g_dma_16bit);

    /* close dsp */
    dsp_dma_close(&g_dsp_dev, g_dma_16bit);
    dsp_close(&g_dsp_dev);

    /* destroy irq sem*/
    sem_destroy(g_irq_sem);
}

void wavdev_play(int play)
{
    dsp_dma_pause(&g_dsp_dev, g_dma_16bit, !play);
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
        while (1) {
            int ret;
            ret = fread(buf, 1, WAVDEV_BUFSIZE, fp);
            if (ret > 0) wavdev_write(buf, ret);
            else break;
        }
        fclose(fp);
    }
    return 0;
}

void main(void)
{
    ffkernel_init();
    wavdev_init(1, 16000, 8);

    task_create(play_task_proc, "test.wav", play_task_ctxt, sizeof(play_task_ctxt));
    wavdev_play(1);
    task_wait(play_task_ctxt, -1);
    wavdev_play(0);
    task_destroy(play_task_ctxt);

    wavdev_exit();
    ffkernel_exit();
}
#endif




