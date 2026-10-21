/******************************************************************************
 * @file    hal_power.h
 * @brief   SC92L7533低功耗硬件抽象层接口
 *****************************************************************************/
#ifndef __HAL_POWER_H
#define __HAL_POWER_H
#include "SC_Init.h"

void HAL_Power_Init(void);
uint16_t HAL_Power_TakeTicks(void);
uint8_t HAL_Power_TakeInputEvent(void);
uint8_t HAL_Power_InputPending(void);
void HAL_Power_Prepare(void);
void HAL_Power_Resume(void);
void HAL_Power_StopOnce(void);
void HAL_Power_BTM_ISR(void);

void HAL_Power_Input_ISR(void);

uint8_t HAL_Power_TypeCIsPresent(void);

#endif