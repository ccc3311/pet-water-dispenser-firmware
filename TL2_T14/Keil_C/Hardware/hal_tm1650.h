#ifndef __HAL_TM1650_H
#define __HAL_TM1650_H

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// --- LED 状态定义 ---
#define LED_OFF 0
#define LED_ON  1


typedef enum {
    // === DIG2: 状态指示灯组 ===
		LED_NO_WATER = 0,   // 缺水警告 (DP1)
	  LED_WATER_Q,        // 水质指示 (G1)
	  LED_HEATING,        // 加热指示 (F1)
	  LED_MODE_CONT,      // 持续模式指示 (E1)
		LED_MODE_SENSE,     // 感应模式指示 (D1)
		LED_SW2,            // 电池电量灯 (C1)	
    LED_SW1,        		// 电源灯 (B1)

    // === DIG3: 感应指示灯组 (相反触发逻辑) ===
    LED_SENSE_IND_1,    // 感应灯 1 (C1)
    LED_SENSE_IND_2,    // 感应灯 2 (D1)
    LED_SENSE_IND_3,    // 感应灯 3 (E1)
    LED_SENSE_IND_4,    // 感应灯 4 (F1)
    LED_SENSE_IND_5     // 感应灯 5 (G1)
} LedId_t;

// --- 外部 API 接口 ---
// 初始化 TM1650，设置亮度并清空屏幕
void HAL_TM1650_Init(void);

// 点亮或熄灭指定的单个 LED
void HAL_LED_SetStatus(LedId_t led_id, uint8_t status);

// 关闭所有 LED (用于关机或深度休眠前)
void HAL_LED_ClearAll(void);

void HAL_TM1650_PowerOff(void); // 【新增】
void HAL_TM1650_PowerOn(void);  // 【新增】

void HAL_LED_SetAllSegaHigh1(void);

void HAL_LED_SetAllSegaHigh2(void);

/**
 * @brief  模拟 I2C 读取一个字节
 * @return 读到的 8 bit 数据
 */
static uint8_t I2C_ReadByte(void);
/**
 * @brief  读取 TM1650 当前的按键状态
 * @return 8位键值。如果没有按键按下，通常返回 0x00 (或其它非按下的释放码)
 */
uint8_t HAL_TM1650_ReadKey(void);


void HAL_TM1650_PowerOff(void);
void HAL_TM1650_PowerOn(void);

#endif /* __HAL_TM1650_H */