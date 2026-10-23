#ifndef __SC92L7533_MIN_H__
#define __SC92L7533_MIN_H__

/*
 * SC92L7533 + Keil C51 最小寄存器定义。
 * 只保留 SC89601 软件 I2C 使用到的 GPIO/ADC 寄存器，地址来自赛元官方
 * SC92L753x_C.H。若工程已经统一包含官方头文件，可改为直接包含官方文件。
 */
sfr P1       = 0x90;
sfr P1CON    = 0x91;
sfr P1PH     = 0x92;
sfr P1VO     = 0x93;
sfr ADCCFG0  = 0xAB;

sbit P16 = P1^6;
sbit P17 = P1^7;

#endif
