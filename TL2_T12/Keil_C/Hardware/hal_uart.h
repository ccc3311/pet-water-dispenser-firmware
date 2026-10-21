#ifndef __HAL_UART_H
#define __HAL_UART_H

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
#endif