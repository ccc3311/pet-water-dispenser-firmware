#include "hal_ir.h"
#include "app_fsm.h"     // 用于操作 g_Device.ir_triggered 总线
#include <stdio.h>

// ==========================================
// 1. 引脚映射修改：适配图2的新硬件
// ==========================================
#define IR1_PIN  P06   // GY

// 软件消抖计数器
static uint8_t ir_debounce_cnt = 0;
extern  volatile uint8_t g_ir_exti_flag;  // 引入外部标志位
/**
 * @brief  红外模块初始化 (适配新硬件)
 */
void HAL_IR_Init(void) {
    // 之前在 SC_GPIO_Init() 里已经将 P0.1 配置为高阻输入了
    ir_debounce_cnt = 0;
}

/**
 * @brief  红外信号扫描与消抖算法 (每 10ms 执行一次)
 */
void HAL_IR_Scan(void) {
    // 1. 读取红外探头的电平
    if (IR1_PIN == 1) {
        // 2. 累加消抖计数器
        if (ir_debounce_cnt < 5) {
            ir_debounce_cnt++;
        }
				// 3. 【核心修复】：只要满足消抖阈值，就必须持续映射硬件的真实电平！
        if (ir_debounce_cnt >= 3) {
            g_Device.ir_triggered = 1; // 向上层总线上报触发事件！
				}
    } else {
        // 4. 一旦检测到低电平，立刻清零计数器
        ir_debounce_cnt = 0;
			g_ir_exti_flag = 0;
        g_Device.ir_triggered = 0;
    }
	}

	/* 任意一路红外为高，表示存在原始感应信号 */
uint8_t HAL_IR_IsActive(void)
{
    return IR1_PIN ? 1 : 0;
}
