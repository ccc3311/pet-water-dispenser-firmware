#ifndef __HAL_BUZZER_H
#define __HAL_BUZZER_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// 蜂鸣器初始化 (配置 P2.5 为推挽输出)
void HAL_Buzzer_Init(void);

// 蜂鸣器基础发声 (阻塞式，用于开机和调试)
// times: 鸣叫次数 (比如 1 就是滴一声，2 就是滴滴两声)
void HAL_Buzzer_Beep(uint8_t times);

//蜂鸣器状态机驱动引擎 (必须在 main.c 中每 1ms 调用一次)
void HAL_Buzzer_Tick_1ms(void);

// 新增：持续3s长鸣（非阻塞）
void HAL_Buzzer_Beep_3s(void);

uint8_t HAL_Buzzer_IsBusy(void);
/* 指定单次鸣叫时长，0表示停止；受静音设置控制。 */
void HAL_Buzzer_BeepMs(uint16_t duration_ms);

#endif /* __HAL_BUZZER_H */
