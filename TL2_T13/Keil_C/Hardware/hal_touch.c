#include "hal_touch.h"
#include "app_fsm.h"
#include "hal_buzzer.h"
#include <stdio.h> 
#include "HeadFiles\SysFunVarDefine.h"
#include "wifi.h"  // 【新增】：必须引入wifi头文件，才能调用 mcu_reset_wifi()
#include "hal_uart.h"
// UVC 外部中断标志位 (保留)
extern volatile uint8_t g_uvc_exti_flag;
// ==========================================
// 硬件引脚映射
// ==========================================
#define TOUCH_MAIN_PIN  P07 
#define UVCING_PIN      P13   

static volatile uint8_t s_touch_busy = 0U;

/* P1.1：短按UV，长按500个10ms节拍请求MCU与Wi-Fi重启。 */
static volatile uint16_t s_uv_hold_10ms = 0U;
static volatile uint8_t s_uv_release_10ms = 0U;
static volatile uint8_t s_uv_wait_release = 1U;
static volatile uint8_t s_uv_started_off = 1U;
static volatile uint8_t s_uv_cooldown_10ms = 0U;
static volatile uint8_t s_uv_key_busy = 0U;
static volatile uint8_t s_touch_restart_pending = 0U;

/* 由原10ms扫描调用；中断中只计时并提交请求。 */
static void HAL_Touch_UvKeyScan10ms(void)
{
    g_uvc_exti_flag = 0U; /* INT0负责唤醒，按键动作改由持续电平判定。 */

    if (P00 == 0) {
        /* 沿用按键掉电屏蔽，避免浮空低电平误触发重启。 */
        s_uv_hold_10ms = 0U;
        s_uv_release_10ms = 0U;
        s_uv_wait_release = 1U;
        s_uv_started_off = 1U;
        s_uv_cooldown_10ms = 0U;
        s_uv_key_busy = 0U;
        return;
    }

    if (s_touch_restart_pending) {
        s_uv_key_busy = 1U;
        return; /* 请求保持到主循环处理，不再提交UV操作。 */
    }

    if (s_uv_cooldown_10ms > 0U) {
        s_uv_cooldown_10ms--;
    }

    if (P11 == 0) {
        s_uv_release_10ms = 0U;
        s_uv_key_busy = 1U;

        if (!s_uv_wait_release && s_uv_hold_10ms < 500U) {
            if (s_uv_hold_10ms == 0U) {
                s_uv_started_off =
                    (g_Device.state == STATE_OFF) ? 1U : 0U;
            }

            if (++s_uv_hold_10ms >= 500U) {
                s_touch_restart_pending = 1U; /* 到5秒只置请求。 */
            }
        }
        return;
    }

    if (s_uv_release_10ms < 3U) {
        s_uv_release_10ms++;
    }
    if (s_uv_release_10ms < 3U) {
        s_uv_key_busy = 1U;
        return;
    }

    if (s_uv_wait_release) {
        s_uv_wait_release = 0U; /* 启动后先确认松开，防止持续按住反复重启。 */
    } else if (s_uv_hold_10ms >= 3U &&
               !s_uv_started_off &&
               g_Device.state != STATE_OFF &&
               s_uv_cooldown_10ms == 0U) {
        g_Device.req_uvc = 1U; /* 松手结算短按，长按重启前不切换UV。 */
        s_uv_cooldown_10ms = 50U;
    }

    s_uv_hold_10ms = 0U;
    s_uv_key_busy = (s_uv_cooldown_10ms != 0U) ? 1U : 0U;
}

/* 合并主按键、UV按键及待处理重启请求，防止按住期间进入STOP。 */
uint8_t HAL_Touch_IsBusy(void)
{
    return (s_touch_busy ||
            s_uv_key_busy ||
            s_touch_restart_pending) ? 1U : 0U;
}

/* 主循环读取；请求不会因松手丢失。 */
uint8_t HAL_Touch_RestartRequested(void)
{
    return s_touch_restart_pending;
}

void HAL_Touch_Init(void)
{
    bit saved_ea;

    saved_ea = EA;
    EA = 0;

    s_touch_busy = 0U;
    s_uv_hold_10ms = 0U;
    s_uv_release_10ms = 0U;
    s_uv_wait_release = 1U;
    s_uv_started_off = 1U;
    s_uv_cooldown_10ms = 0U;
    s_uv_key_busy = 0U;
    s_touch_restart_pending = 0U;
    g_uvc_exti_flag = 0U;

    EA = saved_ea;
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
		/* 记录本次按下时的状态，避免长按期间状态变化触发配网。 */
		static uint8_t key_started_off = 0;
    // 假设按下为低电平
    uint8_t is_pressed = (TOUCH_MAIN_PIN == 0) ? 1 : 0;
	
		HAL_Touch_UvKeyScan10ms(); /* P1.1独立处理，P07主按键继续原流程。 */
	
		    // 【新增核心防护】：P02 断电保护时，强制屏蔽所有按键输入并清空状态缓存
    // 防止触摸芯片断电后引脚浮空，引发“幽灵按键”导致状态机错乱
    if (P00 == 0) {
				key_started_off = 0;
        key_press_timer = 0;
        key_release_timer = 0;
        click_count = 0;
        long_press_3s_done = 0;
        long_press_5s_done = 0;
        g_uvc_exti_flag = 0; // 顺便吃掉 UVC 的中断标志
			    s_touch_busy = 0;       /* 新增 */
        return;
    }
		
    if (is_pressed) {
        if (key_press_timer == 0) {
            key_started_off =
                (g_Device.state == STATE_OFF) ? 1U : 0U;
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

    /* 主按键忙状态；P1.1状态在HAL_Touch_IsBusy中统一合并。 */
    s_touch_busy =
        is_pressed ||
        (key_press_timer != 0) ||
        (click_count != 0);
}

// ==========================================
// UVC驱动区域
// ==========================================
void HAL_UVCING_Start(void)
{
#if !APP_CHARGE_DEBUG
    UVCING_PIN = 1; /* 正常模式允许UV输出；打印模式暂停物理输出。 */
#endif
}

void HAL_UVCING_Stop(void)
{
#if !APP_CHARGE_DEBUG
    UVCING_PIN = 0; /* 与Start共用开关，打印模式不再操作UV引脚。 */
#endif
}
