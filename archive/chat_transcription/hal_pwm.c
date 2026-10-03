#include "hal_pwm.h"

// ==========================================
// 调试开关：强制水泵长转 (投产时务必改为 0 ！)
// ==========================================
#define DEBUG_PUMP_ALWAYS_ON  0  // 1: 开启水泵强制常转无视逻辑, 0: 正常业务控制

// 硬件引脚映射 (仅作示意，实际 PWM 波形由内部模块自动输出到引脚)
#define HEATING_PIN         P10 // Pin 14 加热控制 IO

// 内部状态标志位 (用于判断水泵是否被业务层要求开启)
static uint8_t pump_is_running = 0;

// ==========================================
// 水泵驱动区域 (Pin 18 / PWM2)
// ==========================================

void HAL_PWM_Pump_Start(void) {
    pump_is_running = 1;
    // 启动时不直接赋全速，依赖稳压函数在下一个 100ms 周期自动计算
}

void HAL_PWM_Pump_Stop(void) {
#if DEBUG_PUMP_ALWAYS_ON
    pump_is_running = 1; // 调试模式：拦截一切停泵意图，强行维持运行标志
    P02 = 1;             // 强行保持硬件开启
#else
    pump_is_running = 0;
    P02 = 0; // 占空比直接写 0，硬件瞬间停止输出
#endif
}

/**
 * @brief  水泵动态稳压算法引擎
 * @param  current_sys_mv: 当前系统供电电压(毫伏)，由 ADC 模块采集提供
 * @note   无浮点运算，全整数安全计算
    //现采用任何供电均全速PWM输出
 */
void HAL_PWM_Pump_Constant_Voltage(uint16_t current_sys_mv) {
    uint32_t duty_calc = 0;

#if DEBUG_PUMP_ALWAYS_ON
    pump_is_running = 1; // 强制校正被意外清空的标志位
    P02 = 1;             // 暴力拉高引脚输出
    return;              // 提前退出，完全无视下方业务逻辑
#endif

    // 如果状态机没有要求水泵运行，直接退出
    if (!pump_is_running) {
        P02 = 0;
        return;
    }
    P02 = 1; // 最大占空比不能超过 PWMPRD
}

// ==========================================
// 加热片驱动区域 (Pin 14 / P2.0)
// ==========================================

void HAL_PWM_Heating_Start(void) {
    // 你的配置中加热是普通的推挽 IO (如果你没配成 PWM 的话)
    HEATING_PIN = 1; // 打开 MOS 管开始加热
}

void HAL_PWM_Heating_Stop(void) {
    HEATING_PIN = 0; // 关闭 MOS 管
}

/* 查询业务层是否要求水泵运行 */
uint8_t HAL_PWM_Pump_IsRunning(void)
{
    return pump_is_running;
}
