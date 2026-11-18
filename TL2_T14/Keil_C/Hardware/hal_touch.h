#ifndef __HAL_TOUCH_H
#define __HAL_TOUCH_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// --- 初始化与轮询接口 ---
void HAL_Touch_Init(void);

// 触摸按键扫描与指令解析引擎 (在 main.c 的 10ms 时间片中调用)
void HAL_Touch_Scan(void);

//UVC驱动
void HAL_UVCING_Start(void);

//UVC驱动
void HAL_UVCING_Stop(void); 

uint8_t HAL_Touch_IsBusy(void);

/* P1.1长按5秒提交的重启请求，由主循环处理。 */
uint8_t HAL_Touch_RestartRequested(void);

#endif /* __HAL_TOUCH_H */
