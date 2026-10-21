#include "hal_uart.h"
#include "SC_Init.h"
#include <stdio.h>

// 1. 增加一个 USCI2 专用的软件发送完成标志位
volatile bit g_usci2_tx_done = 1; 

/*
 * 0：本次发送尝试尚未记录底层错误
 * 1：全局中断关闭
 * 2：开始发送时，上一字节尚未完成
 * 3：等待发送完成超时
 */
static unsigned char s_usci2_tx_fault_reason = 0;

unsigned char WiFi_UART_GetTxFaultReason(void)
{
    return s_usci2_tx_fault_reason;
}

/* 一旦发送异常，保持置位，避免继续发送损坏的协议帧 */
static bit s_usci2_tx_fault = 0;

unsigned char WiFi_UART_HasTxFault(void)
{
    return s_usci2_tx_fault ? 1 : 0;
}

void HAL_UART_Init(void) {
    // 调用赛元生成的底层初始化
    SC_UART0_Init();
}

/**
 * @brief  重定向 C 语言标准库的 putchar 函数
 * @note   有了这个函数，你就可以在任何地方直接使用 printf() 了！
 */
char putchar(char c) {
	    uint16_t timeout = 5000;

    // 将单个字符塞入串口发送寄存器
    SBUF = c;
    
    // 等待单字节发送完成，带超时兜底（TI 是 8051 内部的发送完成标志位）
    while (!TI) {
        if (--timeout == 0) break;
    }

    // 软件手动清零，准备发送下一个字符
    TI = 0;      
    
    return c;
}

/*
 * 仅在主循环上下文调用。
 * 调用期间必须允许 USCI2 发送中断执行。
 * printf 使用独立的 UART0 调试串口。
 */
void WiFi_UART_SendByte(unsigned char value)
{
    uint16_t timeout = 5000;
    static unsigned char s_first_timeout_printed = 0;

    /* 本帧已经出现发送故障，不继续发送剩余字节 */
    if (s_usci2_tx_fault) {
        return;
    }

    /* 全局中断关闭，不能启动依赖中断完成的发送 */
    if (!EA) {
        s_usci2_tx_fault_reason = 1;
        s_usci2_tx_fault = 1;
        return;
    }

    /* 上一字节尚未完成，不覆盖发送寄存器 */
    if (!g_usci2_tx_done) {
        s_usci2_tx_fault_reason = 2;
        s_usci2_tx_fault = 1;
        return;
    }

//    USCI2_ClearFlag(USCI2_UART_FLAG_TI);
/* 仅清 TI，保留可能同时到达的 RI。 */
US2CON0 = (US2CON0 & 0xFC) | 0x02;
		
    g_usci2_tx_done = 0;
    USCI2_UART_SendData8(value);

    /* 防卡死计数，5000 不是毫秒数 */
    while (!g_usci2_tx_done) {
        if (--timeout == 0) {
            break;
        }
    }

    /* 等待结束后仍未观察到发送完成，记录故障 */
    if (!g_usci2_tx_done) {
        s_usci2_tx_fault_reason = 3;
        s_usci2_tx_fault = 1;

        /* 每次上电只打印第一次发送等待超时 */
        if (!s_first_timeout_printed) {
            s_first_timeout_printed = 1;

            printf(
                "\r\n[UTX-FIRST] byte=%02X ea=%u done=%u\r\n",
                (unsigned int)value,
                (unsigned int)EA,
                (unsigned int)g_usci2_tx_done);
        }
    }
}

/*
 * 仅在主循环准备发送一帧新数据时调用。
 * 不允许在一帧的中途调用。
 */
unsigned char WiFi_UART_BeginFrame(void)
{
	    s_usci2_tx_fault_reason = 0;

    /* 当前发送依赖中断 */
    if (!EA) {
			        s_usci2_tx_fault_reason = 1;

        return 0;
    }

    /* 上一字节尚未完成，不能启动新帧 */
    if (!g_usci2_tx_done) {
			        s_usci2_tx_fault_reason = 2;

        return 0;
    }

    /*
     * 上一字节已完成，允许新帧重新尝试。
     * 不继续发送上一帧剩余的字节。
     */
    s_usci2_tx_fault = 0;

    return 1;
}

/*
 * 只检查现有发送入口的两个前置条件。
 * 不清故障、不修改完成标志、不写发送寄存器。
 */
unsigned char WiFi_UART_IsTxReady(void)
{
    if (!EA || !g_usci2_tx_done) {
        return 0;
    }

    return 1;
}

/*
 * 初始化 MCU -> Wi-Fi 唤醒脚。
 * 按当前 wkp=0 配置，低电平保持模组工作。
 */
void WiFi_WakeupPin_Init(void)
{
	  /* MCU -> 模组 P24：低电平保持模组工作 */
    /* 先设置输出锁存值，再打开输出 */
    P21  = 0;
    GPIO_Init(GPIO2, GPIO_PIN_1, GPIO_MODE_OUT_PP);
	
		/* 模组 P32 -> MCU：高电平唤醒输入 */
    GPIO_Init(GPIO2, GPIO_PIN_0, GPIO_MODE_IN_HI);
}

/* 请求唤醒，并持续保持模组工作 */
void WiFi_Module_KeepAwake(void)
{
    P21 = 0;
}

/* 释放保持工作条件，不代表模组已经休眠 */
void WiFi_Module_Release(void)
{
    P21 = 1;
}