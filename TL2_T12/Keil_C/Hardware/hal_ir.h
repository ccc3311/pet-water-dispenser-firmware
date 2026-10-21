#ifndef __HAL_IR_H
#define __HAL_IR_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// 红外引脚初始化 (主要依赖赛元生成的 SC_INT_Init)
void HAL_IR_Init(void);

// 10ms 轮询扫描与消抖引擎 (在 main.c 的 10ms 时间片中调用)
void HAL_IR_Scan(void);

uint8_t HAL_IR_IsActive(void);

#endif /* __HAL_IR_H */