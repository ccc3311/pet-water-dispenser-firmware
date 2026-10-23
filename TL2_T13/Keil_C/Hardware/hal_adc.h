#ifndef __HAL_ADC_H
#define __HAL_ADC_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// --- 初始化与轮询接口 ---
void HAL_ADC_Init(void);
void HAL_ADC_Sample_All(void); // 在主循环的 500ms 时间片中调用


// --- 供应用层 (app_fsm.c) 读取的安全数据 ---
// 这比直接暴露全局变量更安全
uint16_t HAL_ADC_Get_SystemVoltage_mV(void);
uint8_t  HAL_ADC_Get_IsEmpty(void);
uint8_t  HAL_ADC_Get_WaterQuality(void); // 返回 WATER_GOOD 或 WATER_BAD
// 新增：获取当前水温 (单位：摄氏度 ℃)
int8_t   HAL_ADC_Get_Temperature_C(void);
uint16_t HAL_ADC_Get_TDS_PPM(void);       // <--- 新增：获取真实的 TDS 值 (ppm)
extern uint8_t HAL_ADC_Get_Battery_SOC(void);

uint8_t HAL_ADC_PowerPoll(uint16_t *battery_mv);
#endif /* __HAL_ADC_H */