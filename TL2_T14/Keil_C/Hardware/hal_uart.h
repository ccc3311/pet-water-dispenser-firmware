#ifndef __HAL_UART_H
#define __HAL_UART_H

/*
 * 硬件模式：
 * 1：启用UART0调试串口，P1.3用于TX，屏蔽UV物理输出。
 * 0：关闭调试串口，恢复UV物理输出。
 */
#define APP_DEBUG_UART_ENABLE  1

/* 兼容原有硬件配置分支，保持当前引脚行为。 */
#define APP_CHARGE_DEBUG  APP_DEBUG_UART_ENABLE

/* 我们自己的日志总开关；涂鸦SDK日志仍独立控制。 */
#define APP_LOG_ENABLE  1

/* 各功能日志开关。 */
#define APP_LOG_CHARGE  0   /* 充电状态、寄存器 */
#define APP_LOG_WIFI    0   /* Wi-Fi状态、配网、通信 */
#define APP_LOG_POWER   1   /* Wi-Fi休眠、MCU STOP */
#define APP_LOG_TIME    0   /* 校时、时间戳 */
#define APP_LOG_BAT     0   /* 当前只调试电量上报 */
/* 最终生效条件：调试串口、总开关、分开关均开启。 */
#define APP_LOG_CHARGE_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_CHARGE)

#define APP_LOG_WIFI_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_WIFI)

#define APP_LOG_POWER_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_POWER)

#define APP_LOG_TIME_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_TIME)
		
#define APP_LOG_BAT_ON \
    (APP_DEBUG_UART_ENABLE && APP_LOG_ENABLE && APP_LOG_BAT)
// 提供一个简单的初始化包装接口
void HAL_UART_Init(void);

#if APP_LOG_POWER_ON
extern volatile bit g_usci2_rx_seen;
#endif

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
