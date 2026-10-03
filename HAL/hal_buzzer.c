#include "hal_buzzer.h"
#include "app_fsm.h"

#define BUZZER_PIN P26

// --- 蜂鸣器状态机内部变量 ---
typedef enum {
    BZ_IDLE = 0,    // 空闲状态
    BZ_ON,          // 正在发声状态
    BZ_OFF_GAP      // 多次鸣叫之间的静音间隔
} BuzzerState_t;

static volatile  BuzzerState_t bz_state = BZ_IDLE; // 当前状态
static uint8_t  bz_remains = 0;          // 还需要鸣叫的次数
static uint16_t bz_timer_ms = 0;         // 内部非阻塞倒计时器

/**
 * @brief  蜂鸣器初始化
 */
void HAL_Buzzer_Init(void) {
    BUZZER_PIN = 0;     // 初始静音
}

/**
 * @brief  发起蜂鸣器鸣叫任务 (非阻塞！瞬间返回！)
 * @param  times: 鸣叫次数
 */
void HAL_Buzzer_Beep(uint8_t times) {
    if (times == 0) return;
if (g_Device.mute_enabled) return; // 【核心】：如果开启了静音，直接憋回去不响
    // 注入任务参数
    bz_remains = times;
    bz_state = BZ_ON;
    bz_timer_ms = 100; // 设定单次发声时间为 100ms

    BUZZER_PIN = 1;    // 立刻开始发声
}

/**
 * @brief  蜂鸣器状态机驱动引擎 (必须在 main.c 中每 1ms 调用一次)
 * @note   绝无死延时，通过每次减 1 实现时间流逝
 */
void HAL_Buzzer_Tick_1ms(void) {
    // 如果处于空闲状态，直接退出，不占用 CPU 算力
    if (bz_state == BZ_IDLE) return;

    // 倒计时递减
    if (bz_timer_ms > 0) {
        bz_timer_ms--;
    }

    // 时间到了，进行状态转移
    if (bz_timer_ms == 0) {
        if (bz_state == BZ_ON) {
            // 发声时间结束，关闭蜂鸣器
            BUZZER_PIN = 0;
            bz_remains--; // 完成了一次鸣叫

            if (bz_remains > 0) {
                // 如果还需要叫，进入静音间隔态
                bz_state = BZ_OFF_GAP;
                bz_timer_ms = 100; // 停顿 100ms 再叫下一声
            } else {
                // 任务彻底完成
                bz_state = BZ_IDLE;
            }
        }
        else if (bz_state == BZ_OFF_GAP) {
            // 间隔停顿时间结束，进入下一次发声
            bz_state = BZ_ON;
            bz_timer_ms = 100; // 发声 100ms
            BUZZER_PIN = 1;
        }
    }
}

/**
 * @brief  发起蜂鸣器 3 秒长鸣任务 (非阻塞！瞬间返回！)
 * @note   利用现有的 tick 引擎，直接注入 3000ms 的发声时间
//修改：实际体感3s太长，修改为800ms
 */
void HAL_Buzzer_Beep_3s(void) {
	// 【核心修复】：长鸣也必须受静音开关控制，防止越权发声
    if (g_Device.mute_enabled) return;
    // 强制覆盖当前可能正在进行的蜂鸣器任务
    bz_remains = 1;        // 只需要叫这一次
    bz_state = BZ_ON;      // 切入发声状态
    bz_timer_ms = 800;    // 设定单次发声时间为 3000ms (3秒)

    BUZZER_PIN = 1;        // 立刻开始发声
}

uint8_t HAL_Buzzer_IsBusy(void)
{
    return (bz_state != BZ_IDLE);
}
