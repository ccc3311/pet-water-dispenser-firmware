#include "hal_touch.h"
#include "app_fsm.h"
#include "hal_buzzer.h"
#include <stdio.h> 
#include "HeadFiles\SysFunVarDefine.h"
#include "wifi.h"  // 【新增】：必须引入wifi头文件，才能调用 mcu_reset_wifi()

// UVC 外部中断标志位 (保留)
extern uint8_t g_uvc_exti_flag;

// ==========================================
// 硬件引脚映射
// ==========================================
#define TOUCH_MAIN_PIN  P07 
#define UVCING_PIN      P51 

/* Timer0扫描更新，主循环读取 */
static volatile uint8_t s_touch_busy = 0;

uint8_t HAL_Touch_IsBusy(void)
{
    return s_touch_busy;
}

void HAL_Touch_Init(void) {
    // 初始化逻辑（如果没有可留空）
}

/**
 * @brief  触摸按键扫描与指令解析 (被 Timer0Interrupt 每 10ms 调用一次)
 */
void HAL_Touch_Scan(void) {
    // ---------------------------------------------------------
    // 1. 主按键 P07 扫描逻辑 (高灵敏度、无阻塞、支持多击与长按)
    // ---------------------------------------------------------
    static uint16_t key_press_timer = 0;
    static uint16_t key_release_timer = 0;
    static uint8_t  click_count = 0;
    static uint8_t  long_press_3s_done = 0;
    static uint8_t  long_press_5s_done = 0;
    static uint8_t uvc_dead_timer = 0;    
		/* 记录本次按下时的状态，避免长按期间状态变化触发配网。 */
		static uint8_t key_started_off = 0;
    // 假设按下为低电平
    uint8_t is_pressed = (TOUCH_MAIN_PIN == 0) ? 1 : 0;

		    // 【新增核心防护】：P02 断电保护时，强制屏蔽所有按键输入并清空状态缓存
    // 防止触摸芯片断电后引脚浮空，引发“幽灵按键”导致状态机错乱
    if (P00 == 0) {
				key_started_off = 0;
        key_press_timer = 0;
        key_release_timer = 0;
        click_count = 0;
        long_press_3s_done = 0;
        long_press_5s_done = 0;
        uvc_dead_timer = 0;
        g_uvc_exti_flag = 0; // 顺便吃掉 UVC 的中断标志
			    s_touch_busy = 0;       /* 新增 */
        return;
    }
		
    if (is_pressed) {
        if (key_press_timer == 0) {
            key_started_off =
                (g_Device.state == STATE_OFF) ? 1 : 0;
        }

        key_release_timer = 0;

        /* 达到最长判定时间后保持，避免持续按住导致计数回绕。 */
        if (key_press_timer < 500U) {
            key_press_timer++;
        }

        /* 沿用现有1秒门槛，松手后提交开关机请求。 */
        if (key_press_timer == 100U) {
            long_press_3s_done = 1;
        }

        /* 只有开机时开始的长按，才允许在5秒触发配网。 */
        if ((key_press_timer == 500U) &&
            !long_press_5s_done &&
            !key_started_off &&
            (g_Device.state != STATE_OFF)) {

            g_Device.req_wifi_reset = 1;

            if (!g_Device.mute_enabled) {
                HAL_Buzzer_Beep_3s();
            }

            long_press_5s_done = 1;
        }
    } else {
        // 刚松手瞬间结算
        /* 松手后区分短按与长按。 */
        if (key_press_timer > 3) {
            if (!long_press_3s_done && !long_press_5s_done) {
                click_count++;

                if (click_count == 2) {
                    if (g_Device.state != STATE_OFF) {
                        if (g_Device.wifi_pairing_timer_s > 0) {
                            g_Device.wifi_pairing_timer_s = 0;
                            g_Device.req_cancel_pairing = 1;

                            if (!g_Device.mute_enabled) {
                                HAL_Buzzer_Beep(2);
                            }
                        } else {
                            g_Device.req_heating = 1;

                            if (!g_Device.mute_enabled) {
                                HAL_Buzzer_Beep(2);
                            }
                        }
                    }

                    click_count = 0;
                }
            } else if (long_press_3s_done &&
                       !long_press_5s_done) {
                /*
                 * 关机开始的长按只允许开机；
                 * 按住期间已被APP开机，则不再反向关机。
                 */
                if (!key_started_off ||
                    (g_Device.state == STATE_OFF)) {
                    g_Device.req_power_toggle = 1;
                }
            }
        }
        // 清理按压状态
        if (key_press_timer > 0) {
            key_press_timer = 0; 
            long_press_3s_done = 0;
            long_press_5s_done = 0;
					  key_started_off = 0;

        }

        // 【单击结算】：只有当处于单击状态，且持续松手 250ms 后，才判定为单击
        if (click_count == 1) {
            key_release_timer++;
            if (key_release_timer >= 25) {
                // 【核心修复：关机状态下屏蔽单击意图】
                if (g_Device.state != STATE_OFF) { 
                    if (g_Device.wifi_pairing_timer_s == 0) {
                        g_Device.req_mode_toggle = 1; 
                    }
                }
                click_count = 0;
                key_release_timer = 0;
            }
        } else {
            key_release_timer = 0;
        }
    }

    // ---------------------------------------------------------
    // 2. 处理 UVC 按键 (外部中断标志位 + 冷却防抖)
    // ---------------------------------------------------------    
    // 每次进入 10ms 轮询时，倒数冷却时间
    if (uvc_dead_timer > 0) {
        uvc_dead_timer--; 
    }

    if (g_Device.state != STATE_OFF) {
        if (g_uvc_exti_flag) {
            // 只有在冷却期结束时，才认为是一次有效的点击
            if (uvc_dead_timer == 0) {
                g_Device.req_uvc = 1;     
                uvc_dead_timer = 50; // 强制进入 500ms 冷却 (50 * 10ms)
            }            
            g_uvc_exti_flag = 0; // 吃掉中断标志位
        }
    } else {
        g_uvc_exti_flag = 0; // 关机状态丢弃 UVC 按键
    }
		 /*
     * 按住、按键结算未完成，或UVC冷却尚未结束，
     * 都保持正常调度，暂不休眠。
     */
    s_touch_busy =
        is_pressed ||
        (key_press_timer != 0) ||
        (click_count != 0) ||
        (uvc_dead_timer != 0);
}

// ==========================================
// UVC驱动区域
// ==========================================
void HAL_UVCING_Start(void) {
    UVCING_PIN = 1; // 打开 MOS 管
}

void HAL_UVCING_Stop(void) {
    UVCING_PIN = 0; // 关闭 MOS 管
}