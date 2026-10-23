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
#define APP_CHARGE_DEBUG  1

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