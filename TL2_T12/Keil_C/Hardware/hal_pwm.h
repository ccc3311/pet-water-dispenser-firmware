#ifndef __HAL_PWM_H
#define __HAL_PWM_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// --- 水泵控制接口 ---
void HAL_PWM_Pump_Start(void);
void HAL_PWM_Pump_Stop(void);
// 核心稳压函数，需在主循环中每 100ms 传入最新的系统电压调用一次
void HAL_PWM_Pump_Constant_Voltage(uint16_t current_sys_mv);

// --- 加热片控制接口 ---
void HAL_PWM_Heating_Start(void);
void HAL_PWM_Heating_Stop(void);

uint8_t HAL_PWM_Pump_IsRunning(void);
#endif /* __HAL_PWM_H */