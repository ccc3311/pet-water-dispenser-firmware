/*
 * Extracted from the user's current main.c (2026-09-16).
 * Stage 3: independent pending mute and switch report values.
 * Other DP reports and production scheduling are not integrated here.
 * All functions run in main-loop context, never in an ISR.
 */
#include "app_wifi.h"
#include "app_fsm.h"
#include "hal_pwm.h"
#include "hal_uart.h"
#include "wifi.h"
#include "protocol.h"
#include <stdio.h>
#include "hal_adc.h"

#define WIFI_TEST_BOOT        0
#define WIFI_TEST_WAIT_SLEEP  1
#define WIFI_TEST_SLEEP       2
#define WIFI_TEST_WAIT_WAKE   3
#define WIFI_TEST_ACTIVE      4
#define WIFI_TEST_ERROR       5

#define WIFI_BAT_REPORT_INTERVAL_S  1800U

#define WIFI_WATER_STABLE_S    60U
#define WIFI_WATER_COOLDOWN_S  1800U
#define WIFI_TDS_STABLE_S    60U
#define WIFI_TDS_COOLDOWN_S  1800U
#define WIFI_PAIRING_HOLD_S  180U
extern uint8_t g_reset_notify_enabled;
extern u8 g_wifi_wake_diag;
/* 待发送位：bit0 WORK_MODE，bit1 PIR，bit2 CONTINUE。 */
static u8 s_wifi_mode_pending = 0;
static u8 s_wifi_mode_value = 0;
static u8 s_wifi_mode_observed = 0;
static u8 s_wifi_mode_retry_s = 0;

static u8 s_wifi_heat_pending = 0;
static u8 s_wifi_heat_retry_s = 0;

/* 本地主动重新配网期间保持模组工作 */
static u8 s_wifi_pairing_hold = 0;
static u8 s_wifi_pairing_seen_disconnect = 0;
static u16 s_wifi_pairing_hold_s = 0;  /* 独立于可被用户取消的 UI 倒计时。 */
static u8 s_wifi_reset_notify_pending = 0;
static u8 s_wifi_reset_notify_retry_s = 0;
static u8 s_wifi_time_pending = 0;
static u8 s_wifi_time_retry_s = 0;

static u8 s_wifi_tds_observed = 0xFF;
static u8 s_wifi_tds_enter_count = 0;
static u8 s_wifi_tds_stable_s = 0;
static u16 s_wifi_tds_cooldown_s = 0;

static u8 s_wifi_tds_pending = 0;
static u8 s_wifi_tds_value = 0;  /* DP：0 正常，1 超标 */
static u8 s_wifi_tds_retry_s = 0;

static u16 s_wifi_bat_cooldown_s = 0;

static u8 s_wifi_test_phase = WIFI_TEST_BOOT;
static u8 s_wifi_test_seconds = 0;
static u8 s_wifi_test_error_printed = 0;
static u8 s_wifi_test_auto_done = 0;
static u8 s_wifi_mute_pending = 0;
static u8 s_wifi_mute_value = 0;
static u8 s_wifi_mute_retry_s = 0;
static u8 s_wifi_switch_pending = 0;
static u8 s_wifi_switch_value = 0;
static u8 s_wifi_switch_retry_s = 0;
/* 最近观察到的实际开关状态，不代表云端已收到 */
static u8 s_wifi_switch_observed = 0;
static u8 s_wifi_bat_observed = 0xFF;
static u8 s_wifi_bat_pending = 0;
static u8 s_wifi_bat_value = 0;
static u8 s_wifi_bat_retry_s = 0;

static u8 s_wifi_temp_high_observed = 0xFF;
static u8 s_wifi_temp_pending = 0;
static u8 s_wifi_temp_value = 0;
static u8 s_wifi_temp_last_sent = 0;
static u8 s_wifi_temp_sent_valid = 0;
static u8 s_wifi_temp_retry_s = 0;
static u8 s_wifi_temp_sync_s = 0;
static u8 s_wifi_temp_sync_due = 0;
static u8 s_wifi_heat_value = 0;
static u8 s_wifi_heat_observed = 0;
static u8 s_wifi_water_observed = 0xFF;
static u8 s_wifi_water_enter_count = 0;
static u8 s_wifi_water_stable_s = 0;
static u16 s_wifi_water_cooldown_s = 0;

static u8 s_wifi_uv_pending = 0;
static u8 s_wifi_uv_value = 0;
static u8 s_wifi_uv_observed = 0;
static u8 s_wifi_uv_retry_s = 0;

static u8 s_wifi_water_pending = 0;
static u8 s_wifi_water_value = 1;  /* 涂鸦：0 缺水，1 正常 */
static u8 s_wifi_water_retry_s = 0;

static u8 s_wifi_clean_days_pending = 0;
static u8 s_wifi_clean_days_retry_s = 0;
static u8 s_wifi_filter_days_pending = 0;
static u8 s_wifi_filter_days_retry_s = 0;

/* 0: none; 1: wake timeout. TX failures retain the pending value. */
static u8 s_wifi_test_error = 0;

void App_WifiTest_Init(void)
{
		s_wifi_reset_notify_pending = 0;
		s_wifi_reset_notify_retry_s = 0;
		s_wifi_time_pending = 0;
		s_wifi_time_retry_s = 0;
		s_wifi_heat_pending = 0;
		s_wifi_heat_retry_s = 0;
		s_wifi_mode_pending = 0;
		s_wifi_mode_value = g_Device.target_work_mode;
		s_wifi_mode_observed = g_Device.target_work_mode;
		s_wifi_mode_retry_s = 0;
		s_wifi_uv_pending = 0;
		s_wifi_uv_value = g_Device.uvc_armed ? 1 : 0;
		s_wifi_uv_observed = s_wifi_uv_value;
		s_wifi_uv_retry_s = 0;
    s_wifi_test_phase = WIFI_TEST_BOOT;
    s_wifi_test_seconds = 0;
    s_wifi_test_error_printed = 0;
    s_wifi_test_auto_done = 0;
    s_wifi_mute_pending = 0;
    s_wifi_mute_value = 0;
    s_wifi_mute_retry_s = 0;
    s_wifi_switch_pending = 0;
    s_wifi_switch_value = 0;
    s_wifi_switch_retry_s = 0;
    s_wifi_test_error = 0;
		s_wifi_heat_value = g_Device.heating_enabled ? 1 : 0;
		s_wifi_heat_observed = s_wifi_heat_value;
		s_wifi_pairing_hold = 0;
		s_wifi_pairing_seen_disconnect = 0;
		s_wifi_pairing_hold_s = 0;

    /* Definitions remain in protocol.c; do not define duplicates here. */
    g_wifi_allow_sleep = 0;
    g_wifi_lp_state = WIFI_LP_UNKNOWN;
    g_wifi_sleep_reason = 0xFF;
	
		s_wifi_switch_observed =
    (g_Device.state != STATE_OFF) ? 1 : 0;
		s_wifi_bat_observed = 0xFF;
		s_wifi_bat_pending = 0;
		s_wifi_bat_value = 0;
		s_wifi_bat_retry_s = 0;
		s_wifi_bat_cooldown_s = 0;
		s_wifi_temp_high_observed = 0xFF;
		s_wifi_temp_pending = 0;
		s_wifi_temp_value = 0;
		s_wifi_temp_last_sent = 0;
		s_wifi_temp_sent_valid = 0;
		s_wifi_temp_retry_s = 0;
		s_wifi_temp_sync_s = 0;
		s_wifi_temp_sync_due = 0;
		s_wifi_water_observed = 0xFF;
		s_wifi_water_enter_count = 0;
		s_wifi_water_stable_s = 0;
		s_wifi_water_cooldown_s = 0;
		s_wifi_water_pending = 0;
		s_wifi_water_value = 1;
		s_wifi_water_retry_s = 0;
		s_wifi_tds_observed = 0xFF;
		s_wifi_tds_enter_count = 0;
		s_wifi_tds_stable_s = 0;
		s_wifi_tds_cooldown_s = 0;
		s_wifi_tds_pending = 0;
		s_wifi_tds_value = 0;
		s_wifi_tds_retry_s = 0;
		s_wifi_clean_days_pending = 0;
		s_wifi_clean_days_retry_s = 0;
		s_wifi_filter_days_pending = 0;
		s_wifi_filter_days_retry_s = 0;
}

u8 App_WifiTest_IsBoot(void)
{
    return (s_wifi_test_phase == WIFI_TEST_BOOT) ? 1 : 0;
}

void App_Wifi_MuteSubmit(u8 value)
{
    s_wifi_mute_value = value ? 1 : 0;
    s_wifi_mute_pending = 1;
    g_wifi_allow_sleep = 0;
}

void App_Wifi_SwitchSubmit(u8 value)
{
    s_wifi_switch_value = value ? 1 : 0;
    s_wifi_switch_pending = 1;
    g_wifi_allow_sleep = 0;
}

static u8 App_Wifi_ReportPending(void)
{
		return (s_wifi_reset_notify_pending ||
            s_wifi_time_pending ||
            s_wifi_mute_pending ||
            s_wifi_switch_pending ||
            s_wifi_mode_pending ||
            s_wifi_heat_pending ||
            s_wifi_bat_pending ||
            s_wifi_water_pending ||
            s_wifi_temp_pending ||
            s_wifi_tds_pending ||
            s_wifi_clean_days_pending ||
		        s_wifi_heat_pending ||
            s_wifi_uv_pending ||
            s_wifi_filter_days_pending) ? 1 : 0;
}

/* 普通 DP 共用发送条件；pending 和重试间隔由各服务判断。 */
static u8 App_Wifi_CanSendDp(void)
{
    if (s_wifi_test_phase != WIFI_TEST_BOOT &&
        s_wifi_test_phase != WIFI_TEST_ACTIVE) {
        return 0;
    }

    if (P21 != 0 || stop_update_flag == ENABLE) {
        return 0;
    }

    return 1;
}

static void App_Wifi_SwitchService(void)
{
    u8 result;

    if (!s_wifi_switch_pending || s_wifi_switch_retry_s) {
        return;
    }
    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_bool_update(DPID_SWITCH, s_wifi_switch_value);
    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_switch_pending = 0;
    } else {
        s_wifi_switch_retry_s = 1;
    }
}

static void App_Wifi_MuteService(void)
{
    u8 result;

    if (!s_wifi_mute_pending || s_wifi_mute_retry_s) {
        return;
    }
    if (!App_Wifi_CanSendDp()) {
			return;
		}


    result = mcu_dp_bool_update(DPID_BEEP, s_wifi_mute_value);
    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_mute_pending = 0;
    } else {
        /* Keep the value; retry on a later software second. */
        s_wifi_mute_retry_s = 1;
    }
}

///* 临时诊断：保持唤醒电平，单次申请休眠以检查模组判断。 */
//static void App_WifiTest_Release(void)
//{
//    static u8 probe_sent = 0;
//    u16 send_len;

//    g_wifi_allow_sleep = 0;
//    WiFi_Module_KeepAwake();

//    if (probe_sent) {
//        return;
//    }

//    if (stop_update_flag == ENABLE ||
//        !WiFi_UART_IsTxReady()) {
//        return;
//    }

//    probe_sent = 1;

//    send_len = set_wifi_uart_byte(0, 0x02);
//    wifi_uart_write_frame(HIBERNATE_CMD, MCU_TX_VER, send_len);

//    printf("\r\n[WIO-TX] p21=%u tx=%u\r\n",
//           (unsigned int)P21,
//           (unsigned int)wifi_uart_last_frame_tx_ok());
//}

static void App_WifiTest_Release(void)
{
    s_wifi_test_seconds = 0;

    /* Type-C 下进入正常通信阶段，保持模组运行。 */
    if (g_Device.power_mode == POWER_AC) {
        g_wifi_allow_sleep = 0;
        WiFi_Module_KeepAwake();
        s_wifi_test_phase = WIFI_TEST_ACTIVE;
        return;
    }

    g_wifi_lp_state = WIFI_LP_UNKNOWN;
    s_wifi_test_phase = WIFI_TEST_WAIT_SLEEP;
    g_wifi_allow_sleep = 1;
    WiFi_Module_Release();
}

static void App_Wifi_BatTick1s(void)
{
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        return;
    }

    if (s_wifi_bat_observed != 2) {
        s_wifi_bat_cooldown_s = 0;
        return;
    }

    /* 已经有电量帧待发送，由原有发送和重试流程处理 */
    if (s_wifi_bat_pending) {
        return;
    }

    if (s_wifi_bat_cooldown_s > 0) {
        s_wifi_bat_cooldown_s--;
    }

    if (s_wifi_bat_cooldown_s == 0) {
        s_wifi_bat_value = 2;
        s_wifi_bat_pending = 1;
        g_wifi_allow_sleep = 0;
    }
}

static void App_Wifi_WaterTick1s(void)
{
    if (s_wifi_water_retry_s > 0) {
        s_wifi_water_retry_s--;
    }

    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        return;
    }

    if (s_wifi_water_cooldown_s > 0) {
        s_wifi_water_cooldown_s--;
    }

    if (s_wifi_water_observed == 1 && g_Device.is_empty) {
        if (s_wifi_water_stable_s < WIFI_WATER_STABLE_S) {
            s_wifi_water_stable_s++;
        }
    } else {
        s_wifi_water_stable_s = 0;
    }
}

static void App_Wifi_TdsTick1s(void)
{
    if (s_wifi_tds_retry_s > 0) {
        s_wifi_tds_retry_s--;
    }

    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        return;
    }

    if (s_wifi_tds_cooldown_s > 0) {
        s_wifi_tds_cooldown_s--;
    }

    if (s_wifi_tds_observed == 1 &&
        g_Device.water_quality == WATER_BAD) {

        if (s_wifi_tds_stable_s < WIFI_TDS_STABLE_S) {
            s_wifi_tds_stable_s++;
        }
    } else {
        s_wifi_tds_stable_s = 0;
    }
}

void App_WifiTest_Tick1s(void)
{
		App_Wifi_BatTick1s();
		App_Wifi_WaterTick1s();
		App_Wifi_TdsTick1s();
		if (s_wifi_pairing_hold && s_wifi_pairing_hold_s > 0) {
			s_wifi_pairing_hold_s--;
		}
		if (s_wifi_uv_retry_s > 0) {
				s_wifi_uv_retry_s--;
		}
		if (s_wifi_time_retry_s > 0) {
    s_wifi_time_retry_s--;
		}
		if (s_wifi_mode_retry_s > 0) {
			s_wifi_mode_retry_s--;
		}
		if (s_wifi_heat_retry_s > 0) {
    s_wifi_heat_retry_s--;
		}
		if (s_wifi_clean_days_retry_s > 0) {
				s_wifi_clean_days_retry_s--;
		}

		if (s_wifi_filter_days_retry_s > 0) {
				s_wifi_filter_days_retry_s--;
		}

		if (s_wifi_temp_retry_s > 0) {
				s_wifi_temp_retry_s--;
		}

		if (s_wifi_test_phase == WIFI_TEST_BOOT) {
				s_wifi_temp_sync_s = 0;
				s_wifi_temp_sync_due = 0;
		} else {
				if (++s_wifi_temp_sync_s >= 60) {
						s_wifi_temp_sync_s = 0;
						s_wifi_temp_sync_due = 1;
				}
		}
    if (s_wifi_mute_retry_s > 0) {
        s_wifi_mute_retry_s--;
    }
    if (s_wifi_switch_retry_s > 0) {
        s_wifi_switch_retry_s--;
    }
		if (s_wifi_bat_retry_s > 0) {
    s_wifi_bat_retry_s--;
		}
		if (s_wifi_reset_notify_retry_s > 0) {
				s_wifi_reset_notify_retry_s--;
		}
		/* 联网后累计启动等待时间。 */
		if (s_wifi_test_phase == WIFI_TEST_BOOT) {
				if (mcu_get_wifi_work_state() != WIFI_CONN_CLOUD) {
						s_wifi_test_seconds = 0;
						return;
				}
		}

    if (s_wifi_test_seconds < 255) {
        s_wifi_test_seconds++;
    }
}

static void App_WifiTest_RunState(void)
{
	u8 wake_from_phase;
u8 wake_from_lp;
u8 wake_from_allow;
	
		/* 待上报、重新配网或接入 Type-C 时唤醒模组。 */
		if ((g_Device.power_mode == POWER_AC ||
				 App_Wifi_ReportPending() ||
				 g_Device.req_wifi_reset) &&
				(s_wifi_test_phase == WIFI_TEST_WAIT_SLEEP ||
				 s_wifi_test_phase == WIFI_TEST_SLEEP)) {
/* 保存切换前状态，供定位唤醒超时。 */
wake_from_phase = s_wifi_test_phase;
wake_from_lp = g_wifi_lp_state;
wake_from_allow = g_wifi_allow_sleep;
				g_wifi_allow_sleep = 0;
				s_wifi_test_seconds = 0;

				if (g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {
						s_wifi_test_phase = WIFI_TEST_ACTIVE;
				} else {
						g_wifi_wake_diag = 0;
						g_wifi_lp_state = WIFI_LP_UNKNOWN;
						s_wifi_test_phase = WIFI_TEST_WAIT_WAKE;
				}

				WiFi_Module_KeepAwake();
				printf(
    "\r\n[WWAKE] from=%u lp=%u allow=%u ac=%u pend=%u reset=%u io=%u\r\n",
    (unsigned int)wake_from_phase,
    (unsigned int)wake_from_lp,
    (unsigned int)wake_from_allow,
    (unsigned int)(g_Device.power_mode == POWER_AC),
    (unsigned int)App_Wifi_ReportPending(),
    (unsigned int)g_Device.req_wifi_reset,
    (unsigned int)P20);
		
				return;
		}

    switch (s_wifi_test_phase) {
		case WIFI_TEST_BOOT:
				if (s_wifi_test_seconds >= 5 &&
						mcu_get_wifi_work_state() == WIFI_CONN_CLOUD &&
						!App_Wifi_ReportPending() &&
						!g_Device.req_wifi_reset &&
						!s_wifi_pairing_hold) {
						App_WifiTest_Release();
				}
				break;

    case WIFI_TEST_WAIT_SLEEP:
        if (g_wifi_lp_state == WIFI_LP_SLEEP_PENDING) {
            s_wifi_test_phase = WIFI_TEST_SLEEP;
            s_wifi_test_seconds = 0;
        }
        else if (g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {
            g_wifi_allow_sleep = 0;
            WiFi_Module_KeepAwake();
            s_wifi_test_phase = WIFI_TEST_ACTIVE;
            s_wifi_test_seconds = 0;
        }
        break;

    case WIFI_TEST_SLEEP:
        if (g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {
            g_wifi_allow_sleep = 0;
            WiFi_Module_KeepAwake();
            s_wifi_test_phase = WIFI_TEST_ACTIVE;
            s_wifi_test_seconds = 0;
            break;
        }

#if WIFI_TEST_AUTO_WAKE
        if (!s_wifi_test_auto_done && s_wifi_test_seconds >= 30) {
            s_wifi_test_auto_done = 1;
            /* Submit asleep; the pending state triggers wake next loop. */
            App_Wifi_MuteSubmit(g_Device.mute_enabled);
        }
#endif
        break;
				
case WIFI_TEST_WAIT_WAKE:
    g_wifi_allow_sleep = 0;
    WiFi_Module_KeepAwake();

    if (g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {
        s_wifi_test_phase = WIFI_TEST_ACTIVE;
        s_wifi_test_seconds = 0;
    }
    else if (s_wifi_test_seconds >= 5) {
        s_wifi_test_error = 1;
        s_wifi_test_phase = WIFI_TEST_ERROR;
    }
    break;
//case WIFI_TEST_WAIT_WAKE:
//    g_wifi_allow_sleep = 0;

//    if (g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {
//        /* 通知可能在重试的高电平期间到达，先恢复保持工作。 */
//        WiFi_Module_KeepAwake();
//        s_wifi_test_phase = WIFI_TEST_ACTIVE;
//        s_wifi_test_seconds = 0;
//    }
//    else if (s_wifi_test_seconds >= 11) {
//        /* 一次重试仍无通知，保留原有错误处理。 */
//        WiFi_Module_KeepAwake();
//        s_wifi_test_error = 1;
//        s_wifi_test_phase = WIFI_TEST_ERROR;
//    }
//    else if (s_wifi_test_seconds >= 6) {
//        WiFi_Module_KeepAwake();
//    }
//    else if (s_wifi_test_seconds >= 5) {
//        /* 临时验证：拉高约1秒，为第二次下降沿做准备。 */
//        WiFi_Module_Release();
//    }
//    break;

    case WIFI_TEST_ACTIVE:
        if (App_Wifi_ReportPending() ||
				g_Device.req_wifi_reset ||
				s_wifi_pairing_hold) {
            g_wifi_allow_sleep = 0;
            WiFi_Module_KeepAwake();
            s_wifi_test_seconds = 0;
        }
        else if (s_wifi_test_seconds >= 5) {
            App_WifiTest_Release();
        }
        break;

		case WIFI_TEST_ERROR:
				/*
				 * 唤醒超时后，如果又收到新的唤醒通知，
				 * 且 hibernate_fun() 已成功发送唤醒 ACK，
				 * 则恢复通信窗口。
				 *
				 * 仅处理 err=1，不处理 err=3 重置帧发送失败。
				 */
				if (s_wifi_test_error == 1 &&
						g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) {

						g_wifi_allow_sleep = 0;
						WiFi_Module_KeepAwake();

						s_wifi_test_error = 0;
						s_wifi_test_error_printed = 0;
						s_wifi_test_seconds = 0;
						s_wifi_test_phase = WIFI_TEST_ACTIVE;
						break;
				}

				/* 尚未满足恢复条件：保持唤醒，不发送业务数据 */
				g_wifi_allow_sleep = 0;
				WiFi_Module_KeepAwake();

				if (!s_wifi_test_error_printed) {
						s_wifi_test_error_printed = 1;
					printf(
							"\r\n[WTEST] err=%u lp=%u expected=%u txok=%u wake=%u p21=%u p20=%u\r\n",
							(unsigned int)s_wifi_test_error,
							(unsigned int)g_wifi_lp_state,
							(unsigned int)WIFI_LP_WAKE_NOTIFIED,
							(unsigned int)wifi_uart_last_frame_tx_ok(),
							(unsigned int)g_wifi_wake_diag,
							(unsigned int)P21,
							(unsigned int)P20);
				}
				break;

		default:
				break;
		}   /* switch 结束 */
}   /* App_WifiTest_RunState() 结束 */

static void App_Wifi_SwitchObserve(void)
{
    u8 current_on;

    current_on = (g_Device.state != STATE_OFF) ? 1 : 0;

    /* 启动阶段继续沿用原来的云端同步 */
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_switch_observed = current_on;
        return;
    }

    if (current_on != s_wifi_switch_observed) {
        s_wifi_switch_observed = current_on;

        /* 只提交实际状态，不操作设备，不直接发送 */
        App_Wifi_SwitchSubmit(current_on);
    }
}

static void App_Wifi_BatObserve(void)
{
    u8 current_soc;

    /* 使用现有算法处理后的档位：0 高、1 中、2 低 */
    current_soc = HAL_ADC_Get_Battery_SOC();

    if (current_soc > 2) {
        return;
    }

    if (s_wifi_test_phase == WIFI_TEST_BOOT ||
    s_wifi_bat_observed == 0xFF) {
    s_wifi_bat_observed = current_soc;

    /*
     * 启动首报仍由原有业务负责。
     * 启动结束后，低电量等待一个完整周期再重复上报。
     */
    s_wifi_bat_cooldown_s =
        (current_soc == 2) ? WIFI_BAT_REPORT_INTERVAL_S : 0;

    return;
		}

		/* 电量恢复后，解除低电量重复上报计时 */
		if (current_soc != 2) {
				s_wifi_bat_cooldown_s = 0;
		}

    if (current_soc != s_wifi_bat_observed) {
        s_wifi_bat_observed = current_soc;

        /* 独立保存最新档位，发送失败时继续保留 */
        s_wifi_bat_value = current_soc;
        s_wifi_bat_pending = 1;
        g_wifi_allow_sleep = 0;
    }
}

static void App_Wifi_BatService(void)
{
    u8 result;

    if (!s_wifi_bat_pending || s_wifi_bat_retry_s) {
        return;
    }

		if (!App_Wifi_CanSendDp()) {
				return;
		}

    result = mcu_dp_enum_update(
        DPID_BAT, s_wifi_bat_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
    s_wifi_bat_pending = 0;

    s_wifi_bat_cooldown_s =
        (s_wifi_bat_value == 2) ?
        WIFI_BAT_REPORT_INTERVAL_S : 0;
		} else {
				s_wifi_bat_retry_s = 1;
		}
}

static void App_Wifi_TempObserve(void)
{
    u8 current_temp;
    u8 is_high;
    u8 need_report;

    current_temp = HAL_ADC_Get_Temperature_C();
    is_high = (current_temp > 38) ? 1 : 0;
    need_report = 0;

    /* 启动同步仍由原有业务负责 */
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_temp_high_observed = is_high;
        return;
    }

    if (s_wifi_temp_high_observed == 0xFF) {
        s_wifi_temp_high_observed = is_high;
        need_report = 1;
    } else if (is_high != s_wifi_temp_high_observed) {
        s_wifi_temp_high_observed = is_high;
        need_report = 1;
    }

    if (s_wifi_temp_sync_due) {
        s_wifi_temp_sync_due = 0;

        if (!s_wifi_temp_sent_valid ||
            current_temp != s_wifi_temp_last_sent) {
            need_report = 1;
        }
    }

    if (need_report) {
        s_wifi_temp_pending = 1;
        g_wifi_allow_sleep = 0;
    }

    /* 等待唤醒或重试期间，保留最新实际温度 */
    if (s_wifi_temp_pending) {
        s_wifi_temp_value = current_temp;
    }
}

static void App_Wifi_TempService(void)
{
    u8 result;

    if (!s_wifi_temp_pending || s_wifi_temp_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_value_update(
        DPID_TEMP_CURRENT, (u32)s_wifi_temp_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_temp_last_sent = s_wifi_temp_value;
        s_wifi_temp_sent_valid = 1;
        s_wifi_temp_pending = 0;
    } else {
        s_wifi_temp_retry_s = 1;
    }
}

static void App_Wifi_WaterObserve(void)
{
    u8 current_empty;

    current_empty = g_Device.is_empty ? 1 : 0;

    /* BOOT 阶段继续沿用原有启动同步 */
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_water_observed = current_empty;
        s_wifi_water_enter_count = 0;
        s_wifi_water_stable_s = 0;
        s_wifi_water_cooldown_s = 0;
        return;
    }

    if (s_wifi_water_observed == 0xFF) {
        s_wifi_water_observed = current_empty;
    }
    else if (current_empty != s_wifi_water_observed) {
        s_wifi_water_observed = current_empty;
        s_wifi_water_stable_s = 0;

        if (current_empty) {
            /* 只统计“正常 -> 缺水”，最多累计到 10 */
            if (s_wifi_water_enter_count < 10) {
                s_wifi_water_enter_count++;
            }

            /* 水位再次缺水，取消尚未发出的过时恢复帧 */
            if (s_wifi_water_pending &&
                s_wifi_water_value == 1) {
                s_wifi_water_pending = 0;
            }
        } else {
            /* 恢复正常：解除冷却，但保留累计进入次数 */
            s_wifi_water_cooldown_s = 0;

            /*
             * 已上报过缺水，或已有报警等待发送时，
             * 提交/覆盖为正常状态。
             */
            if (g_Device_Shadow.is_empty != 0 ||
                s_wifi_water_pending) {
                s_wifi_water_value = 1;
                s_wifi_water_pending = 1;
                g_wifi_allow_sleep = 0;
            }
        }
    }

    if (current_empty &&
        s_wifi_water_cooldown_s == 0 &&
        (s_wifi_water_enter_count >= 10 ||
         s_wifi_water_stable_s >= WIFI_WATER_STABLE_S)) {

        s_wifi_water_value = 0;
        s_wifi_water_pending = 1;
        g_wifi_allow_sleep = 0;
    }
}


static void App_Wifi_WaterService(void)
{
    u8 result;

    if (!s_wifi_water_pending || s_wifi_water_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_enum_update(
        DPID_WATER_LEVEL, s_wifi_water_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_water_pending = 0;

        g_Device_Shadow.is_empty =
            (s_wifi_water_value == 0) ? 1 : 0;

        if (s_wifi_water_value == 0) {
            /* 缺水报警本地发送完成后，才开始新周期 */
            s_wifi_water_enter_count = 0;
            s_wifi_water_stable_s = 0;
            s_wifi_water_cooldown_s = WIFI_WATER_COOLDOWN_S;
        } else {
            s_wifi_water_cooldown_s = 0;
        }
    } else {
        s_wifi_water_retry_s = 1;
    }
}

static void App_Wifi_TdsObserve(void)
{
    u8 current_bad;

    current_bad =
        (g_Device.water_quality == WATER_BAD) ? 1 : 0;

    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_tds_observed = current_bad;
        s_wifi_tds_enter_count = 0;
        s_wifi_tds_stable_s = 0;
        s_wifi_tds_cooldown_s = 0;
        return;
    }

    if (s_wifi_tds_observed == 0xFF) {
        s_wifi_tds_observed = current_bad;
    }
    else if (current_bad != s_wifi_tds_observed) {
        s_wifi_tds_observed = current_bad;
        s_wifi_tds_stable_s = 0;

        if (current_bad) {
            if (s_wifi_tds_enter_count < 10) {
                s_wifi_tds_enter_count++;
            }

            /* 取消尚未发送的过时正常状态 */
            if (s_wifi_tds_pending &&
                s_wifi_tds_value == 0) {
                s_wifi_tds_pending = 0;
            }
        } else {
            /* 恢复正常，保留累计进入超标的次数 */
            s_wifi_tds_cooldown_s = 0;

            if (g_Device_Shadow.water_quality != WATER_GOOD ||
                s_wifi_tds_pending) {
                s_wifi_tds_value = 0;
                s_wifi_tds_pending = 1;
                g_wifi_allow_sleep = 0;
            }
        }
    }

    if (current_bad &&
        s_wifi_tds_cooldown_s == 0 &&
        (s_wifi_tds_enter_count >= 10 ||
         s_wifi_tds_stable_s >= WIFI_TDS_STABLE_S)) {

        s_wifi_tds_value = 1;
        s_wifi_tds_pending = 1;
        g_wifi_allow_sleep = 0;
    }
}

static void App_Wifi_TdsService(void)
{
    u8 result;

    if (!s_wifi_tds_pending || s_wifi_tds_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_enum_update(
        DPID_TDS_RO, s_wifi_tds_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_tds_pending = 0;

        g_Device_Shadow.water_quality =
            s_wifi_tds_value ? WATER_BAD : WATER_GOOD;

        if (s_wifi_tds_value == 1) {
            s_wifi_tds_enter_count = 0;
            s_wifi_tds_stable_s = 0;
            s_wifi_tds_cooldown_s = WIFI_TDS_COOLDOWN_S;
        } else {
            s_wifi_tds_cooldown_s = 0;
        }
    } else {
        s_wifi_tds_retry_s = 1;
    }
}

void App_Wifi_CleanDaysSubmit(void)
{
    s_wifi_clean_days_pending = 1;
    g_wifi_allow_sleep = 0;
}

void App_Wifi_FilterDaysSubmit(void)
{
    s_wifi_filter_days_pending = 1;
    g_wifi_allow_sleep = 0;
}

static void App_Wifi_CleanDaysService(void)
{
    u8 result;

    if (!s_wifi_clean_days_pending || s_wifi_clean_days_retry_s) {
        return;
    }

		if (!App_Wifi_CanSendDp()) {
				return;
		}

    result = mcu_dp_value_update(
        DPID_TIME_CLEAN, (u32)g_Device.target_clean_days);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_clean_days_pending = 0;
    } else {
        s_wifi_clean_days_retry_s = 1;
    }
}

static void App_Wifi_FilterDaysService(void)
{
    u8 result;

    if (!s_wifi_filter_days_pending || s_wifi_filter_days_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_value_update(
        DPID_FILTER_LIFE, (u32)g_Device.target_filter_days);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_filter_days_pending = 0;
    } else {
        s_wifi_filter_days_retry_s = 1;
    }
}

static u8 App_Wifi_ResetService(void)
{
	static u8 s_reset_diag_printed = 0;
    if (!g_Device.req_wifi_reset) {
    s_reset_diag_printed = 0;
    return 0;
		}

		/* 每个重置请求只打印一次，位于阶段判断之前 */
		if (!s_reset_diag_printed) {
				s_reset_diag_printed = 1;

				printf(
						"\r\n[WR] p=%u e=%u h=%u d=%u n=%u t=%u "
						"lp=%u io=%u r=%u on=%u ready=%u fault=%u\r\n",
						(unsigned int)s_wifi_test_phase,
						(unsigned int)s_wifi_test_error,
						(unsigned int)s_wifi_pairing_hold,
						(unsigned int)s_wifi_pairing_seen_disconnect,
						(unsigned int)mcu_get_wifi_work_state(),
						(unsigned int)g_Device.wifi_pairing_timer_s,
						(unsigned int)g_wifi_lp_state,
						(unsigned int)P21,
						(unsigned int)g_reset_notify_enabled,
						(unsigned int)(g_Device.state != STATE_OFF),
						(unsigned int)WiFi_UART_IsTxReady(),
						(unsigned int)WiFi_UART_HasTxFault());
		}

		/*
		 * 正常接受 BOOT / ACTIVE。
		 * err=3 后，仅在存在新的按键重置请求时允许再次尝试。
		 * 函数开头已经检查 req_wifi_reset。
		 */
		if (s_wifi_test_phase != WIFI_TEST_BOOT &&
				s_wifi_test_phase != WIFI_TEST_ACTIVE &&
				!(s_wifi_test_phase == WIFI_TEST_ERROR &&
					s_wifi_test_error == 3)) {
				return 0;
		}

		if (P21 != 0 || stop_update_flag == ENABLE) {
				return 0;
		}

		/*
		 * 尚未具备开始新帧的条件：
		 * 保留重置请求，交还主循环，继续处理串口和其他任务。
		 */
		if (!WiFi_UART_IsTxReady()) {
				return 0;
		}

		/* 新请求已经具备发送条件，清除上一轮错误记录 */
		s_wifi_test_error = 0;
		s_wifi_test_error_printed = 0;

    g_Device.req_wifi_reset = 0;
    g_wifi_allow_sleep = 0;

    s_wifi_pairing_hold = 1;
    s_wifi_pairing_seen_disconnect = 0;
    s_wifi_test_phase = WIFI_TEST_ACTIVE;
    s_wifi_test_seconds = 0;

    WiFi_Module_KeepAwake();
    mcu_reset_wifi();

		if (!wifi_uart_last_frame_tx_ok()) {
				/*
				 * 立即记录，避免下一帧 BeginFrame() 刷新诊断原因。
				 * printf 使用现有独立调试串口。
				 */
				printf(
						"\r\n[WR-TX] reason=%u\r\n",
						(unsigned int)WiFi_UART_GetTxFaultReason());

				s_wifi_test_error = 3;
				s_wifi_test_error_printed = 0;
				s_wifi_test_phase = WIFI_TEST_ERROR;
				return 1;
		}

		g_reset_notify_enabled = 0;
		g_Device.user_cancelled_pairing = 0;
		g_Device.wifi_pairing_timer_s = WIFI_PAIRING_HOLD_S;
		s_wifi_pairing_hold_s = WIFI_PAIRING_HOLD_S;

    return 1;
}

/* 配网期间保持工作；确认重新连云或保持时间到期后，恢复普通通信调度。 */
static void App_Wifi_PairingTask(void)
{
    u8 current_wifi_state;

    if (!s_wifi_pairing_hold ||
        s_wifi_test_phase == WIFI_TEST_ERROR) {
        return;
    }

    g_wifi_allow_sleep = 0;
    WiFi_Module_KeepAwake();

    current_wifi_state = mcu_get_wifi_work_state();

    /* 先看到配网或断开状态，避免把重置前的连接缓存当作本次配网成功。 */
    if (current_wifi_state == 0 ||
        current_wifi_state == 1 ||
        current_wifi_state == 2 ||
        current_wifi_state == 6) {
        s_wifi_pairing_seen_disconnect = 1;
    }

    if (s_wifi_pairing_seen_disconnect &&
        current_wifi_state == WIFI_CONN_CLOUD) {
        printf("\r\n[WPAIR] connected\r\n");
    } else if (s_wifi_pairing_hold_s == 0) {
        printf("\r\n[WPAIR] timeout\r\n");
    } else {
        return;
    }

    s_wifi_pairing_hold = 0;
    s_wifi_pairing_seen_disconnect = 0;
    s_wifi_pairing_hold_s = 0;

    /* 后续由 ACTIVE 状态完成待发任务，并按原来的空闲时间释放模组。 */
    s_wifi_test_phase = WIFI_TEST_ACTIVE;
    s_wifi_test_seconds = 0;
}

/* 观察本地实际模式；变化时覆盖尚未完成的旧上报。 */
static void App_Wifi_ModeObserve(void)
{
    u8 current_mode;

    current_mode = g_Device.target_work_mode;
    if (current_mode > 1) {
        return;
    }

    /* BOOT 阶段继续使用原有云端同步。 */
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_mode_observed = current_mode;
        return;
    }

    if (current_mode != s_wifi_mode_observed) {
        s_wifi_mode_observed = current_mode;
        s_wifi_mode_value = current_mode;
        s_wifi_mode_pending = 0x07;
        s_wifi_mode_retry_s = 0;
        g_wifi_allow_sleep = 0;
    }
}

/* 每次只发送一个模式 DP，三帧均本地发送成功后更新影子。 */
static void App_Wifi_ModeService(void)
{
    u8 result;
    u8 sent_mask;

    if (!s_wifi_mode_pending || s_wifi_mode_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    if (s_wifi_mode_pending & 0x01) {
        sent_mask = 0x01;
        result = mcu_dp_enum_update(
            DPID_WORK_MODE,
            s_wifi_mode_value == 1 ? 0 : 1);
    }
    else if (s_wifi_mode_pending & 0x02) {
        sent_mask = 0x02;
        result = mcu_dp_bool_update(
            DPID_PIR,
            s_wifi_mode_value == 0 ? 1 : 0);
    }
    else {
        sent_mask = 0x04;
        result = mcu_dp_bool_update(
            DPID_CONTINUE,
            s_wifi_mode_value == 1 ? 1 : 0);
    }

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_mode_pending &= (u8)(~sent_mask);

        if (s_wifi_mode_pending == 0) {
            g_Device_Shadow.target_work_mode =
                s_wifi_mode_value;
        }
    } else {
        /* 保留当前帧及后续帧，等待下一次重试。 */
        s_wifi_mode_retry_s = 1;
    }
}

/* BOOT 期间只跟踪设置，后续变化交给待发服务上报。 */
static void App_Wifi_HeatObserve(void)
{
    u8 current_heat;

    current_heat = g_Device.heating_enabled ? 1 : 0;

    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_heat_observed = current_heat;
        return;
    }

    if (current_heat != s_wifi_heat_observed) {
        s_wifi_heat_observed = current_heat;
        s_wifi_heat_value = current_heat;
        s_wifi_heat_pending = 1;
        s_wifi_heat_retry_s = 0;
        g_wifi_allow_sleep = 0;
    }
}

/* 发送缓存的加热设置，失败后保留待发并延时重试。 */
static void App_Wifi_HeatService(void)
{
    u8 result;

    if (!s_wifi_heat_pending || s_wifi_heat_retry_s) {
        return;
    }

    if (s_wifi_test_phase != WIFI_TEST_BOOT &&
        s_wifi_test_phase != WIFI_TEST_ACTIVE) {
        return;
    }

    if (P21 != 0 || stop_update_flag == ENABLE) {
        return;
    }

    result = mcu_dp_bool_update(DPID_HEAT, s_wifi_heat_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        g_Device_Shadow.heating_enabled = s_wifi_heat_value;
        s_wifi_heat_pending = 0;
    } else {
        s_wifi_heat_retry_s = 1;
    }
}

/* 同步 UVC 武装设置，不跟随杀菌灯的实际启停。 */
static void App_Wifi_UvObserve(void)
{
    u8 current_uv;

    current_uv = g_Device.uvc_armed ? 1 : 0;

    /* BOOT 阶段继续沿用原有云端同步。 */
    if (s_wifi_test_phase == WIFI_TEST_BOOT) {
        s_wifi_uv_observed = current_uv;
        return;
    }

    if (current_uv != s_wifi_uv_observed) {
        s_wifi_uv_observed = current_uv;
        s_wifi_uv_value = current_uv;
        s_wifi_uv_pending = 1;
        s_wifi_uv_retry_s = 0;
        g_wifi_allow_sleep = 0;
    }
}

/* 本地发送完成后更新影子；失败保留最新待上报值。 */
static void App_Wifi_UvService(void)
{
    u8 result;

    if (!s_wifi_uv_pending || s_wifi_uv_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
			return;
		}

    result = mcu_dp_bool_update(
        DPID_UV, s_wifi_uv_value);

    if (result == SUCCESS && wifi_uart_last_frame_tx_ok()) {
        s_wifi_uv_pending = 0;
        g_Device_Shadow.uvc_armed = s_wifi_uv_value;
    } else {
        s_wifi_uv_retry_s = 1;
    }
}

/* 合并时间同步请求，由主循环协调唤醒和发送。 */
void App_Wifi_TimeSyncSubmit(void)
{
    s_wifi_time_pending = 1;
    g_wifi_allow_sleep = 0;
}

/* 本地发送失败保留请求；时间响应仍由原有回调处理。 */
static void App_Wifi_TimeSyncService(void)
{
    if (!s_wifi_time_pending || s_wifi_time_retry_s) {
        return;
    }

		if (s_wifi_test_phase != WIFI_TEST_BOOT &&
				s_wifi_test_phase != WIFI_TEST_ACTIVE) {
				return;
		}

		if (P21 != 0 || stop_update_flag == ENABLE) {
				return;
		}

    mcu_get_green_time();

    if (wifi_uart_last_frame_tx_ok()) {
        s_wifi_time_pending = 0;
    } else {
        s_wifi_time_retry_s = 1;
    }
}

/* 合并注册需求，重复提交不重置重试间隔。 */
void App_Wifi_ResetNotifySubmit(void)
{
    if (g_reset_notify_enabled) {
        return;
    }

    s_wifi_reset_notify_pending = 1;
    g_wifi_allow_sleep = 0;
}

/* 沿用现有协议处理：结果为 0 表示注册成功。 */
void App_Wifi_ResetNotifyResult(u8 result)
{
    if (result == 0) {
        g_reset_notify_enabled = 1;
        s_wifi_reset_notify_pending = 0;
        s_wifi_reset_notify_retry_s = 0;
    } else {
        g_reset_notify_enabled = 0;
        s_wifi_reset_notify_pending = 1;
        s_wifi_reset_notify_retry_s = 5;
        g_wifi_allow_sleep = 0;
    }

}

/* 发送完成后继续等待模组确认，不能直接判定注册成功。 */
static void App_Wifi_ResetNotifyService(void)
{
    u16 send_len;
    u8 tx_ok;

    if (!s_wifi_reset_notify_pending ||
        s_wifi_reset_notify_retry_s) {
        return;
    }

    if (s_wifi_test_phase != WIFI_TEST_BOOT &&
        s_wifi_test_phase != WIFI_TEST_ACTIVE) {
        return;
    }

    if (P21 != 0 || stop_update_flag == ENABLE) {
        return;
    }

    send_len = 0;
    send_len = set_wifi_uart_byte(send_len, 0x04);

    wifi_uart_write_frame(
        MODULE_EXTEND_FUN_CMD, MCU_TX_VER, send_len);

    tx_ok = wifi_uart_last_frame_tx_ok() ? 1 : 0;
    s_wifi_reset_notify_retry_s = tx_ok ? 5 : 1;

    printf("\r\n[WREG] tx=%u\r\n",
           (unsigned int)tx_ok);
}

void App_WifiTest_Task(void)
{
    App_Wifi_SwitchObserve();
		App_Wifi_ModeObserve();
    App_Wifi_BatObserve();
	  App_Wifi_HeatObserve();
    App_Wifi_UvObserve();
		App_Wifi_TempObserve();
    App_Wifi_WaterObserve();
		App_Wifi_TdsObserve();
App_Wifi_PairingTask();

/* 联网后补交注册需求，不依赖主动查询状态的回调。 */
if (mcu_get_wifi_work_state() == WIFI_CONN_CLOUD &&
    !g_reset_notify_enabled) {
    App_Wifi_ResetNotifySubmit();
}

App_WifiTest_RunState();

    /* 本轮发送过重置帧，就不继续发送其他帧。 */
    if (App_Wifi_ResetService()) {
        return;
    }

    /* 注册属于协议维护，配网保持期间也允许处理。 */
    if (!g_Device.req_wifi_reset &&
        s_wifi_reset_notify_pending &&
        !s_wifi_reset_notify_retry_s) {
        App_Wifi_ResetNotifyService();
        return;
    }

    /* 等待重置或配网过程中，保留其他待发送需求。 */
    if (g_Device.req_wifi_reset || s_wifi_pairing_hold) {
        return;
    }

    /* 本轮尝试时间请求后，不继续发送业务帧。 */
    if (s_wifi_time_pending && !s_wifi_time_retry_s) {
        App_Wifi_TimeSyncService();
        return;
    }
    /* 每轮最多尝试发送一帧。 */
    if (s_wifi_switch_pending && !s_wifi_switch_retry_s) {
        App_Wifi_SwitchService();
    } else if (s_wifi_mode_pending && !s_wifi_mode_retry_s) {
        App_Wifi_ModeService();
    } else if (s_wifi_heat_pending && !s_wifi_heat_retry_s) {
        App_Wifi_HeatService();
    } else if (s_wifi_uv_pending && !s_wifi_uv_retry_s) {
        App_Wifi_UvService();
    } else if (s_wifi_mute_pending && !s_wifi_mute_retry_s) {
        App_Wifi_MuteService();
    } else if (s_wifi_water_pending && !s_wifi_water_retry_s) {
        App_Wifi_WaterService();
    } else if (s_wifi_tds_pending && !s_wifi_tds_retry_s) {
        App_Wifi_TdsService();
    } else if (s_wifi_bat_pending && !s_wifi_bat_retry_s) {
        App_Wifi_BatService();
    } else if (s_wifi_temp_pending && !s_wifi_temp_retry_s) {
        App_Wifi_TempService();
    } else if (s_wifi_clean_days_pending &&
               !s_wifi_clean_days_retry_s) {
        App_Wifi_CleanDaysService();
    } else {
        App_Wifi_FilterDaysService();
    }
}

void App_WifiTest_OnDpReceived(void)
{
    /*
     * 有效 DP 到达，沿用现有设计进入通信窗口。
     * 新增：
     * 1. 等待唤醒期间收到 DP；
     * 2. 唤醒超时之后收到 DP。
     *
     * err=3 等其他错误不在这里处理。
     */
    if (s_wifi_test_phase == WIFI_TEST_WAIT_SLEEP ||
        s_wifi_test_phase == WIFI_TEST_SLEEP ||
        s_wifi_test_phase == WIFI_TEST_WAIT_WAKE ||
        s_wifi_test_phase == WIFI_TEST_ACTIVE ||
        (s_wifi_test_phase == WIFI_TEST_ERROR &&
         s_wifi_test_error == 1)) {

        g_wifi_allow_sleep = 0;
        WiFi_Module_KeepAwake();

        s_wifi_test_phase = WIFI_TEST_ACTIVE;
        s_wifi_test_seconds = 0;
        s_wifi_test_error = 0;
        s_wifi_test_error_printed = 0;
    }
}

/* 有效远程重置通知到达后，保持模组工作以完成重新配网。 */
void App_Wifi_OnRemoteReset(void)
{
    /*
     * 复用已有通信窗口恢复规则。
     * err=3 不会在这里被清除。
     */
    App_WifiTest_OnDpReceived();
		/* 新一轮远程配网重新启用提示，与本地配网入口保持一致。 */
		g_Device.user_cancelled_pairing = 0;
		g_Device.wifi_pairing_timer_s = WIFI_PAIRING_HOLD_S;
	
    g_wifi_allow_sleep = 0;
    WiFi_Module_KeepAwake();

		s_wifi_pairing_hold = 1;
		s_wifi_pairing_seen_disconnect = 0;
		s_wifi_pairing_hold_s = WIFI_PAIRING_HOLD_S;
		s_wifi_test_seconds = 0;

}

/* MCU 仅在模组进入睡眠阶段、没有通信待办时申请 STOP。 */
u8 App_Wifi_McuStopReady(void)
{
    if (s_wifi_test_phase != WIFI_TEST_SLEEP ||
        s_wifi_pairing_hold ||
        g_Device.req_wifi_reset ||
        s_wifi_reset_notify_pending ||
        s_wifi_time_pending ||
        App_Wifi_ReportPending()) {
        return 0;
    }

    if (P21 == 0 || P20 != 0 ||
        g_wifi_lp_state != WIFI_LP_SLEEP_PENDING) {
        return 0;
    }

    if (WiFi_UART_HasTxFault() ||
        !wifi_uart_last_frame_tx_ok()) {
        return 0;
    }

    return 1;
}