#ifndef __HAL_UART_H
#define __HAL_UART_H

/*
 * 打印 / UV 功能切换，修改后重新编译整个工程：
 *
 * 1：开启充电寄存器打印，暂停UV物理输出。
 * 0：关闭调试打印，恢复UV物理输出及原MCU STOP流程。
 *
 * 打印模式只屏蔽物理UV输出，按键和APP的设置逻辑仍然运行。
 */
/* UART0/UV硬件模式独立于应用日志开关。 */
#define APP_DEBUG_UART_ENABLE  1
#define APP_CHARGE_DEBUG       APP_DEBUG_UART_ENABLE

/* 应用日志总开关；涂鸦SDK日志继续使用其原有开关。 */
#define APP_LOG_ENABLE  1
#define APP_LOG_CHARGE  0
#define APP_LOG_WIFI    1
#define APP_LOG_POWER   1
#define APP_LOG_TIME    0

#define APP_LOG_CHARGE_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_CHARGE)
#define APP_LOG_WIFI_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_WIFI)
#define APP_LOG_POWER_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_POWER)
#define APP_LOG_TIME_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_TIME)

// 提供一个简单的初始化包装接口
void HAL_UART_Init(void);

//涂鸦wifi函数（自写）
void WiFi_UART_SendByte(unsigned char value);

unsigned char WiFi_UART_HasTxFault(void);

unsigned char WiFi_UART_BeginFrame(void);

void WiFi_WakeupPin_Init(void);
void WiFi_Module_KeepAwake(void);
void WiFi_Module_Release(void);
unsigned char WiFi_UART_GetTxFaultReason(void);
unsigned char WiFi_UART_IsTxReady(void);

void WiFi_CEN_Release(void);
void WiFi_CEN_Assert(void);
unsigned char WiFi_CEN_IsHeld(void);
void WiFi_UART_Reinit(void);

#endif