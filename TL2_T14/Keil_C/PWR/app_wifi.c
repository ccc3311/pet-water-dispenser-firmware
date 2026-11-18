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
#include "sc89601.h"
#define WIFI_CEN_PIN_TEST  0  /* 旧板波形测试；正常版本设为0。 */
extern volatile bit g_usci2_tx_done;

#define WIFI_TEST_BOOT        0
#define WIFI_TEST_WAIT_SLEEP  1
#define WIFI_TEST_SLEEP       2
#define WIFI_TEST_WAIT_WAKE   3
#define WIFI_TEST_ACTIVE      4
#define WIFI_TEST_ERROR       5
#define WIFI_TEST_CEN_LOW   6
#define WIFI_TEST_CEN_WAIT  7
#define WIFI_TEST_CEN_COOL  8
#define WIFI_TEST_BAT_OFF  9  /* 低电保护主动关闭模组。 */

#define WIFI_BAT_REPORT_INTERVAL_S  1800U

#define WIFI_WATER_STABLE_S    60U
#define WIFI_WATER_RECOVER_S  10U
#define WIFI_TDS_STABLE_S    60U
#define WIFI_TDS_COOLDOWN_S  1800U
#define WIFI_PAIRING_HOLD_S  180U

#define WIFI_BOOT_OFFLINE_WAIT_S  180U

#define WIFI_TEMP_HIGH_C             37U
#define WIFI_TEMP_RECOVER_C          37U
#define WIFI_TEMP_RECOVER_CONFIRM_S  10U
#define WIFI_TEMP_RECOVER_IDLE       0xFFU

static void App_Wifi_WaterObserve(void);
static u8 s_wifi_boot_wait_s = 0;
static u8 s_wifi_boot_cloud_done = 0;

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

/* 高温状态：0正常，1已锁存，0xFF尚未建立初值。 */
static u8 s_wifi_temp_high_observed = 0xFF;
static u8 s_wifi_temp_mod = WIFI_TEMP_MOD_DEFAULT;  /* 0 摄氏，1 华氏 */

/* 恢复确认倒计时：0xFF未启动，0表示确认时间已到。 */
static u8 s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;

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
static u8 s_wifi_water_stable_s = 0;

static u8 s_wifi_uv_pending = 0;
static u8 s_wifi_uv_value = 0;
static u8 s_wifi_uv_observed = 0;
static u8 s_wifi_uv_retry_s = 0;

static u8 s_wifi_water_pending = 0;
static u8 s_wifi_water_value = 0xFF; /* 0缺水、1正常、FF未确认。 */
static u8 s_wifi_water_retry_s = 0;

static u8 s_wifi_clean_days_pending = 0;
static u8 s_wifi_clean_days_retry_s = 0;
static u8 s_wifi_filter_days_pending = 0;
static u8 s_wifi_filter_days_retry_s = 0;

/* 1：唤醒未确认，包含唤醒超时及休眠阶段发送故障。 */
static u8 s_wifi_test_error = 0;

/* 重建通信会话，保留所有业务待发位。 */
static void App_Wifi_CenStart(u8 next_phase)
{
    g_wifi_allow_sleep = 0;
    WiFi_Module_KeepAwake();
    WiFi_CEN_Assert(); /* 先禁止收发，再清理旧会话。 */
    wifi_protocol_init();

    g_wifi_lp_state = WIFI_LP_UNKNOWN;
    g_wifi_sleep_reason = 0xFF;
    g_wifi_wake_diag = 0;

    g_reset_notify_enabled = 0;
    s_wifi_reset_notify_pending = 1; /* 模组重启后重新注册通知。 */
    s_wifi_reset_notify_retry_s = 0;
    s_wifi_time_pending = 1;
    s_wifi_time_retry_s = 0;

    s_wifi_test_phase = next_phase;
    s_wifi_test_seconds = 0; /* 复用已有阶段计时。 */
}

/* 工作状态帧的 ACK 本地发送完成，恢复待发调度。 */
void App_Wifi_OnWorkStateReady(void)
{
    if (s_wifi_test_phase != WIFI_TEST_CEN_WAIT ||
        WiFi_CEN_IsHeld()) {
        return; /* 只处理本次 CEN 重启后的握手完成。 */
    }

    s_wifi_test_phase = WIFI_TEST_ACTIVE;
    s_wifi_test_seconds = 0;
    s_wifi_test_error = 0;
    s_wifi_test_error_printed = 0;

    g_wifi_allow_sleep = 0;
    WiFi_Module_KeepAwake();
}

void App_WifiTest_Init(void)
{
		s_wifi_boot_wait_s = 0;      /* 独立统计启动等待，不受联网计时清零影响。 */
		s_wifi_boot_cloud_done = 0; /* 首次连云后仍需执行原启动同步。 */
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
		s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;

		s_wifi_temp_pending = 0;
		s_wifi_temp_value = 0;
		s_wifi_temp_last_sent = 0;
		s_wifi_temp_sent_valid = 0;
		s_wifi_temp_retry_s = 0;
		s_wifi_temp_sync_s = 0;
		s_wifi_temp_sync_due = 0;
		s_wifi_water_observed = 0xFF;
		s_wifi_water_stable_s = 0;
		s_wifi_water_pending = 0;
		s_wifi_water_value = 0xFF;
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

void App_Wifi_TempModSubmit(unsigned char mode)
{
    s_wifi_temp_mod = mode ? 1U : 0U;

    /* 切换单位时，把最新温度与单位选择同帧回传。 */
    s_wifi_temp_pending = 1U;
    s_wifi_temp_retry_s = 0U;
    g_wifi_allow_sleep = 0U;
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

/* 返回1表示SDK及本地整帧发送均成功，不代表云端确认。 */
static u8 App_Wifi_DpTxSucceeded(u8 result)
{
    /* 共用11处判定以减少重复代码；SDK失败时仍直接返回0。 */
    return ((result == SUCCESS) &&
            wifi_uart_last_frame_tx_ok()) ? 1 : 0;
}

/*
 * 旧上报路径的结果处理：
 * 成功提交影子；失败保存对应DP的待发请求。
 * 耗材补报仍由现有服务读取最新剩余天数。
 */
static u8 App_Wifi_ReportResult(u8 dpid, u8 value, u8 result)
{
    u8 tx_ok;
    u8 mode;
    u8 sent_mask;

    /* 修复：暂停上传时，不能把上一帧的成功标志当成本次成功。 */
    tx_ok = (stop_update_flag != ENABLE &&
             App_Wifi_DpTxSucceeded(result)) ? 1U : 0U;

    switch (dpid) {
    case DPID_SWITCH:
        if (!tx_ok) {
            /* APP请求尚未执行时，保留其已排队的目标回传。 */
            if (!s_wifi_switch_pending ||
                !g_Device.req_power_toggle) {
                App_Wifi_SwitchSubmit(value);
            }
        } else {
            if (!g_Device.req_power_toggle ||
                s_wifi_switch_value == value) {
                s_wifi_switch_pending = 0;
            }

            if (value == (g_Device.state != STATE_OFF)) {
                g_Device_Shadow.state = g_Device.state;
            }
        }
        break;

    case DPID_WORK_MODE:
    case DPID_PIR:
    case DPID_CONTINUE:
        sent_mask = (dpid == DPID_WORK_MODE) ? 0x01U :
                    ((dpid == DPID_PIR) ? 0x02U : 0x04U);

        mode = (dpid == DPID_CONTINUE) ?
               (value ? 1U : 0U) : (value ? 0U : 1U);

        if (s_wifi_mode_pending &&
            s_wifi_mode_value != mode) {
            /* 新模式替换旧模式，重新补齐三项关联DP。 */
            s_wifi_mode_pending = 0x07U;
        }

        s_wifi_mode_value = mode;
        s_wifi_mode_retry_s = 0;

        if (tx_ok) {
            s_wifi_mode_pending &= (u8)(~sent_mask);
        } else {
            /* 修复：任一帧失败，交给原模式待发服务补齐。 */
            s_wifi_mode_pending = 0x07U;
        }
        break;

    case DPID_HEAT:
        s_wifi_heat_value = value;
        s_wifi_heat_pending = tx_ok ? 0U : 1U;
        s_wifi_heat_retry_s = 0;
        if (tx_ok) {
            g_Device_Shadow.heating_enabled = value;
        }
        break;

    case DPID_UV:
        s_wifi_uv_value = value;
        s_wifi_uv_pending = tx_ok ? 0U : 1U;
        s_wifi_uv_retry_s = 0;
        if (tx_ok) {
            g_Device_Shadow.uvc_armed = value;
        }
        break;

    case DPID_TDS_RO:
        s_wifi_tds_value = value;
        s_wifi_tds_pending = tx_ok ? 0U : 1U;
        s_wifi_tds_retry_s = 0;
        if (tx_ok) {
            g_Device_Shadow.water_quality =
                value ? WATER_BAD : WATER_GOOD;
        }
        break;

    case DPID_TEMP_CURRENT:
        s_wifi_temp_value = value;
        s_wifi_temp_pending = tx_ok ? 0U : 1U;
        s_wifi_temp_retry_s = 0;
        if (tx_ok) {
            s_wifi_temp_last_sent = value;
            s_wifi_temp_sent_valid = 1;
        }
        break;

    case DPID_BAT:
#if APP_LOG_BAT_ON
    /* 记录发送当时的网络状态，避免仅看UART成功。 */
    printf("[BTX] value=%u ok=%u net=%u cloud=%u\r\n",
        (unsigned int)value,
        (unsigned int)tx_ok,
        (unsigned int)mcu_get_wifi_work_state(),
        (unsigned int)(
            mcu_get_wifi_work_state() == WIFI_CONN_CLOUD));
#endif
        s_wifi_bat_value = value;
        s_wifi_bat_pending = tx_ok ? 0U : 1U;
        s_wifi_bat_retry_s = 0;

        if (tx_ok) {
            /* 启动或查询发送成功后，也同步观察记录，避免重复排队。 */
            s_wifi_bat_observed = value;
            s_wifi_bat_cooldown_s =
                (value == WIFI_BAT_LOW) ?
                WIFI_BAT_REPORT_INTERVAL_S : 0U;
        }
        break;

    case DPID_BEEP:
        s_wifi_mute_value = value;
        s_wifi_mute_pending = tx_ok ? 0U : 1U;
        s_wifi_mute_retry_s = 0;
        if (tx_ok) {
            g_Device_Shadow.mute_enabled = value;
        }
        break;

    case DPID_TIME_CLEAN:
        s_wifi_clean_days_pending = tx_ok ? 0U : 1U;
        s_wifi_clean_days_retry_s = 0;
        break;

    case DPID_FILTER_LIFE:
        s_wifi_filter_days_pending = tx_ok ? 0U : 1U;
        s_wifi_filter_days_retry_s = 0;
        break;

    default:
        return tx_ok ? SUCCESS : ERROR;
    }

    if (!tx_ok) {
        /* 修复：补报完成前，继续使用原有唤醒和恢复流程。 */
        g_wifi_allow_sleep = 0;
    }

    return tx_ok ? SUCCESS : ERROR;
}

/* 保留SDK接口，由应用层补充发送确认和失败记账。 */
u8 App_Wifi_ReportBool(u8 dpid, u8 value)
{
    return App_Wifi_ReportResult(
        dpid, value ? 1U : 0U,
        mcu_dp_bool_update(dpid, value));
}

u8 App_Wifi_ReportEnum(u8 dpid, u8 value)
{
    return App_Wifi_ReportResult(
        dpid, value,
        mcu_dp_enum_update(dpid, value));
}

/* 同一帧上报摄氏度和华氏度，输入沿用现有整数摄氏度。 */
static u8 App_Wifi_TempDpUpdate(u8 temp_c)
{
    u16 temp_f;
    u16 send_len;
    u8 i;
    u8 dp_value;

    /* 两个Value型DP共占16字节数据区。 */
    if (stop_update_flag == ENABLE ||
        WIFIR_UART_SEND_BUF_LMT < 21U) {
        return ERROR;
    }

    /* F = C * 9 / 5 + 32；加2实现非负整数四舍五入。 */
    temp_f = ((u16)temp_c * 9U + 2U) / 5U + 32U;

    /* 与平台DP111的当前上限保持一致。 */
    if (temp_f > 150U) {
        temp_f = 150U;
    }

    send_len = 0U;

    for (i = 0U; i < 2U; i++) {
        dp_value = (i == 0U) ? temp_c : (u8)temp_f;

        send_len = set_wifi_uart_byte(
            send_len,
            (i == 0U) ? DPID_TEMP_CURRENT : DPID_TEMP_HUA);
        send_len = set_wifi_uart_byte(send_len, DP_TYPE_VALUE);
        send_len = set_wifi_uart_byte(send_len, 0U);
        send_len = set_wifi_uart_byte(send_len, 4U);

        /* Value固定4字节，大端；当前温度范围只用最低字节。 */
        send_len = set_wifi_uart_byte(send_len, 0U);
        send_len = set_wifi_uart_byte(send_len, 0U);
        send_len = set_wifi_uart_byte(send_len, 0U);
        send_len = set_wifi_uart_byte(send_len, dp_value);
    }

    /* DP112 为 Bool，长度 1 字节，0 表示摄氏，1 表示华氏。 */
    send_len = set_wifi_uart_byte(send_len, DPID_TEMP_MOD);
    send_len = set_wifi_uart_byte(send_len, DP_TYPE_BOOL);
    send_len = set_wifi_uart_byte(send_len, 0U);
    send_len = set_wifi_uart_byte(send_len, 1U);
    send_len = set_wifi_uart_byte(send_len, s_wifi_temp_mod);
		
    wifi_uart_write_frame(
        STATE_UPLOAD_CMD, MCU_TX_VER, send_len);
			 
    /* 仅表示本地整帧发送结果，供原有补报逻辑使用。 */
    return wifi_uart_last_frame_tx_ok() ? SUCCESS : ERROR;
}

u8 App_Wifi_ReportValue(u8 dpid, u32 value)
{
    u8 result;

    if (dpid == DPID_TEMP_CURRENT) {
        result = App_Wifi_TempDpUpdate((u8)value);
    } else {
        result = mcu_dp_value_update(dpid, value);
    }

    return App_Wifi_ReportResult(dpid, (u8)value, result);
}

/* 三帧均本地发送成功后，才提交整组模式影子。 */
void App_Wifi_ReportMode(void)
{
    u8 mode;
    u8 tx_ok;

    mode = g_Device.target_work_mode;
    tx_ok = SUCCESS;

    if (App_Wifi_ReportEnum(
            DPID_WORK_MODE, mode == 1 ? 0 : 1) != SUCCESS) {
        tx_ok = ERROR;
    }

    if (App_Wifi_ReportBool(
            DPID_PIR, mode == 0) != SUCCESS) {
        tx_ok = ERROR;
    }

    if (App_Wifi_ReportBool(
            DPID_CONTINUE, mode == 1) != SUCCESS) {
        tx_ok = ERROR;
    }

    if (tx_ok == SUCCESS) {
        g_Device_Shadow.target_work_mode = mode;
    }
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

				/* 修复：回传目标已实际满足时，补报成功同步提交影子。 */
				if (s_wifi_switch_value == (g_Device.state != STATE_OFF)) {
						g_Device_Shadow.state = g_Device.state;
				}
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
				/* 修复：成功补报后更新影子，失败继续保留原记录。 */
				g_Device_Shadow.mute_enabled = s_wifi_mute_value;
		} else {
				s_wifi_mute_retry_s = 1;
		}
}

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

/* BTM秒任务调用，正常运行与STOP巡检期间均推进确认。 */
void App_Wifi_WaterTick1s(void)
{
    u8 limit_s;
    u8 value;

    if (s_wifi_water_retry_s > 0U) {
        s_wifi_water_retry_s--;
    }

    App_Wifi_WaterObserve();

    limit_s = s_wifi_water_observed ?
        WIFI_WATER_STABLE_S : WIFI_WATER_RECOVER_S;

    if (s_wifi_water_stable_s < limit_s) {
        s_wifi_water_stable_s++;
    }

    if (s_wifi_water_stable_s >= limit_s) {
        value = s_wifi_water_observed ? 0U : 1U;

        if (s_wifi_water_value != value) {
            s_wifi_water_value = value;
            s_wifi_water_pending = 1;
            s_wifi_water_retry_s = 0; /* 新确认状态替换旧状态的重试。 */
            g_wifi_allow_sleep = 0;
        }
    }
}

/* 同步已确认水位，不重置确认计时或发送重试间隔。 */
void App_Wifi_WaterSyncSubmit(void)
{
    if (s_wifi_water_value <= 1U) {
        s_wifi_water_pending = 1;
        g_wifi_allow_sleep = 0;
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

/* 主循环秒任务调用：限频打印休眠阶段和阻塞待办。 */
static void App_Wifi_LpDebug1s(void)
{
#if APP_LOG_POWER_ON
    static u8 divider = 0;
    u16 pending;

    if (++divider < 5U) {
        return;
    }
    divider = 0;
#if APP_LOG_CHARGE_ON
/* 同时观察原始档位、映射结果及充电判断条件。 */
printf(
    "\r\n[BAT] raw=%u dp=%u ac=%u init=%u poll=%u\r\n",
    (unsigned int)HAL_ADC_Get_Battery_SOC(),
    (unsigned int)App_Wifi_GetBatDpValue(),
    (unsigned int)(g_Device.power_mode == POWER_AC),
    (unsigned int)g_sc89601_init_result,
    (unsigned int)g_sc89601_poll_result);

printf(
    "[BCH] vb=%u pg=%u st=%u top=%u en=%u fault=%02X\r\n",
    (unsigned int)g_sc89601_snapshot.vbus_good,
    (unsigned int)g_sc89601_snapshot.power_good,
    (unsigned int)g_sc89601_snapshot.charge_status,
    (unsigned int)(
        (g_sc89601_snapshot.reg[SC89601_REG_STAT2] & 0x08U) != 0U),
    (unsigned int)(
        (g_sc89601_snapshot.reg[SC89601_REG_CTL1] & 0x10U) != 0U),
    (unsigned int)g_sc89601_snapshot.fault_current);
#endif
    /* 位图保留具体待办来源，便于定位是谁阻止休眠。 */
    pending = 0;
    if (s_wifi_reset_notify_pending) pending |= 0x0001U; /* 注册 */
    if (s_wifi_time_pending)         pending |= 0x0002U; /* 校时 */
    if (s_wifi_mute_pending)         pending |= 0x0004U;
    if (s_wifi_switch_pending)       pending |= 0x0008U;
    if (s_wifi_mode_pending)         pending |= 0x0010U;
    if (s_wifi_heat_pending)         pending |= 0x0020U;
    if (s_wifi_bat_pending)          pending |= 0x0040U;
    if (s_wifi_water_pending)        pending |= 0x0080U;
    if (s_wifi_temp_pending)         pending |= 0x0100U;
    if (s_wifi_tds_pending)          pending |= 0x0200U;
    if (s_wifi_clean_days_pending)   pending |= 0x0400U;
    if (s_wifi_uv_pending)           pending |= 0x0800U;
    if (s_wifi_filter_days_pending)  pending |= 0x1000U;

    printf(
        "\r\n[WLP] p=%u net=%u boot=%u lp=%u allow=%u io=%u%u\r\n",
        (unsigned int)s_wifi_test_phase,
        (unsigned int)mcu_get_wifi_work_state(),
        (unsigned int)s_wifi_boot_wait_s,
        (unsigned int)g_wifi_lp_state,
        (unsigned int)g_wifi_allow_sleep,
        (unsigned int)P21,
        (unsigned int)P20);

    printf(
        "[WPD] q=%04X rst=%u w=%u rx=%u cen=%u tx=%u fault=%u\r\n",
        (unsigned int)pending,
        (unsigned int)g_Device.req_wifi_reset,
        (unsigned int)g_wifi_wake_diag,
        (unsigned int)g_usci2_rx_seen,
        (unsigned int)P51,
        (unsigned int)WiFi_UART_IsTxReady(),
        (unsigned int)WiFi_UART_HasTxFault());
#endif
}
/* 每5秒观察映射值、网络状态和待发状态。 */
static void App_Wifi_BatDebug1s(void)
{
#if APP_LOG_BAT_ON
    static u8 divider = 0U;

    if (++divider < 5U) {
        return;
    }
    divider = 0U;

    printf(
        "\r\n[BAT] dp=%u net=%u p=%u io=%u q=%u\r\n",
        (unsigned int)App_Wifi_GetBatDpValue(),
        (unsigned int)mcu_get_wifi_work_state(),
        (unsigned int)s_wifi_test_phase,
        (unsigned int)P21,
        (unsigned int)s_wifi_bat_pending);
#endif
}
void App_WifiTest_Tick1s(void)
{
#if WIFI_CEN_PIN_TEST
    static u8 s_cen_pin_test_s = 0;

    if (s_cen_pin_test_s < 10) {
        s_cen_pin_test_s++;
    }

    if (s_cen_pin_test_s == 10 &&
        stop_update_flag != ENABLE &&
        s_wifi_test_phase == WIFI_TEST_ACTIVE &&
        WiFi_UART_IsTxReady() &&
        !WiFi_UART_HasTxFault()) {

        s_cen_pin_test_s = 11; /* 每次上电只注入一次，避免恢复后重复触发。 */
        g_usci2_tx_done = 0;   /* 当前无发送字节，模拟发送完成标志无法恢复。 */
        return;
    }
#endif
/* 恢复计时只推进一次；温度回升时由观察函数取消。 */
if (s_wifi_temp_recover_wait_s != WIFI_TEMP_RECOVER_IDLE &&
    s_wifi_temp_recover_wait_s > 0U) {
    s_wifi_temp_recover_wait_s--;
}

		if (s_wifi_test_phase == WIFI_TEST_BOOT &&
    s_wifi_boot_wait_s < WIFI_BOOT_OFFLINE_WAIT_S) {
    s_wifi_boot_wait_s++; /* 未连云时也推进启动等待，避免永久停在BOOT。 */
		}
			 
		/* 放在离线分支的return之前，保证未联网时也能打印。 */
		App_Wifi_LpDebug1s();
		App_Wifi_BatDebug1s();
		App_Wifi_BatTick1s();
		App_Wifi_TdsTick1s();
		if (s_wifi_pairing_hold && s_wifi_pairing_hold_s > 0) {
			s_wifi_pairing_hold_s--;
		}
		if (s_wifi_uv_retry_s > 0) {
				s_wifi_uv_retry_s--;
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
		
		if (stop_update_flag == ENABLE) {
			if (s_wifi_test_phase == WIFI_TEST_ERROR) {
					s_wifi_test_seconds = 0; /* 升级期间暂停异常复位计时。 */
			}
			return;
		}

    /* 修复：休眠相关阶段也检查UART故障，接入统一恢复流程。 */
    if ((s_wifi_test_phase == WIFI_TEST_BOOT ||
         s_wifi_test_phase == WIFI_TEST_ACTIVE ||
         s_wifi_test_phase == WIFI_TEST_WAIT_WAKE ||
         s_wifi_test_phase == WIFI_TEST_WAIT_SLEEP ||
         s_wifi_test_phase == WIFI_TEST_SLEEP) &&
        (!WiFi_UART_IsTxReady() || WiFi_UART_HasTxFault())) {

        if (s_wifi_test_phase == WIFI_TEST_WAIT_SLEEP ||
            s_wifi_test_phase == WIFI_TEST_SLEEP) {
            s_wifi_test_error = 1; /* 休眠链路异常，按唤醒未确认处理。 */
            g_wifi_lp_state = WIFI_LP_UNKNOWN; /* 清除旧握手，等待新的通信依据。 */
        } else {
            s_wifi_test_error =
                (s_wifi_test_phase == WIFI_TEST_BOOT) ? 4 : 2;
        }

        s_wifi_test_phase = WIFI_TEST_ERROR;
        s_wifi_test_seconds = 0; /* 从本次故障开始累计原有的30秒恢复窗口。 */
    }
		
		if (!s_wifi_boot_cloud_done &&
				s_wifi_test_phase == WIFI_TEST_ACTIVE &&
				mcu_get_wifi_work_state() == WIFI_CONN_CLOUD) {

				s_wifi_test_phase = WIFI_TEST_BOOT; /* 离线启动后首次连云，补回原启动同步。 */
				s_wifi_test_seconds = 0;            /* 重新累计联网后的5秒窗口。 */
		}
		
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
					#if APP_LOG_POWER_ON
							g_usci2_rx_seen = 0;
					#endif
						g_wifi_wake_diag = 0;
						g_wifi_lp_state = WIFI_LP_UNKNOWN;
						s_wifi_test_phase = WIFI_TEST_WAIT_WAKE;
				}

				WiFi_Module_KeepAwake();
				return;
		}

    switch (s_wifi_test_phase) {
		case WIFI_TEST_BOOT:
    if (s_wifi_test_seconds >= 5 &&
        mcu_get_wifi_work_state() == WIFI_CONN_CLOUD &&
        !App_Wifi_ReportPending() &&
        !g_Device.req_wifi_reset &&
        !s_wifi_pairing_hold) {

        s_wifi_boot_cloud_done = 1; /* 联网启动窗口完成，后续不重复进入BOOT。 */
        App_WifiTest_Release();
    }
    else if (s_wifi_boot_wait_s >= WIFI_BOOT_OFFLINE_WAIT_S &&
             mcu_get_wifi_work_state() != WIFI_CONN_CLOUD &&
             !App_Wifi_ReportPending() &&
             !g_Device.req_wifi_reset &&
             !s_wifi_pairing_hold) {

        App_WifiTest_Release(); /* 离线等待到期且无待办，交给原休眠握手处理。 */
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
							s_wifi_test_seconds = 0; /* 从进入错误阶段开始累计 30 秒。 */
						s_wifi_test_phase = WIFI_TEST_ERROR;
				}
				break;

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
				g_wifi_allow_sleep = 0;
				WiFi_Module_KeepAwake();

				if (WiFi_UART_IsTxReady() && !WiFi_UART_HasTxFault() &&
						((s_wifi_test_error == 1 &&
							g_wifi_lp_state == WIFI_LP_WAKE_NOTIFIED) ||
						 s_wifi_test_error == 2 || s_wifi_test_error == 4)) {

						s_wifi_test_phase =
								(s_wifi_test_error == 4) ?
								WIFI_TEST_BOOT : WIFI_TEST_ACTIVE;

						s_wifi_test_error = 0;
						s_wifi_test_error_printed = 0;
						s_wifi_test_seconds = 0; /* 异常自行恢复，取消硬件复位。 */
				}
				else if (s_wifi_test_seconds >= 30) {
						App_Wifi_CenStart(WIFI_TEST_CEN_LOW);
				}
				break;

		case WIFI_TEST_CEN_LOW:
				if (s_wifi_test_seconds >= 2) {
						WiFi_UART_Reinit();
						wifi_protocol_init(); /* 再清一次复位期间的接收残留。 */

						s_wifi_test_seconds = 0;
						s_wifi_test_phase = WIFI_TEST_CEN_WAIT;
#if APP_LOG_POWER_ON
    g_usci2_rx_seen = 0;
#endif
						WiFi_CEN_Release(); /* 串口准备完成后再启动模组。 */
				}
				break;

		case WIFI_TEST_CEN_WAIT:
				if (s_wifi_test_seconds >= 30) {
						App_Wifi_CenStart(WIFI_TEST_CEN_COOL);
				}
				break;

		case WIFI_TEST_CEN_COOL:
				if (s_wifi_test_seconds >= 240) {
						App_Wifi_CenStart(WIFI_TEST_CEN_LOW); /* 冷却后再次尝试启动。 */
				}
				break;

		default:
				break;
		}
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

/* 组合已有ADC档位与充电快照，不执行额外I2C读取。 */
u8 App_Wifi_GetBatDpValue(void)
{
    u8 level;
    u8 charge_status;
/* 跟随实际保护锁存，避免电压防抖期间提前上报保护。 */
if (App_FSM_BatteryProtectActive()) {
    return WIFI_BAT_PROTECT;
}
    level = HAL_ADC_Get_Battery_SOC();
    if (level > WIFI_BAT_LOW) {
        return WIFI_BAT_INVALID;
    }

    /* 未就绪、拔电或读取失败时，不用旧快照显示正在充电。 */
    if (g_Device.power_mode != POWER_AC ||
        g_sc89601_init_result != SC89601_OK ||
        g_sc89601_poll_result != SC89601_OK ||
        !g_sc89601_snapshot.vbus_good ||
        !g_sc89601_snapshot.power_good ||
        (g_sc89601_snapshot.fault_current & 0xFCU) != 0U) {
        return level;
    }

    charge_status = g_sc89601_snapshot.charge_status;

    /* 阶段3且补充充电已结束，才按充满显示High。 */
    if (charge_status == 3U &&
        (g_sc89601_snapshot.reg[SC89601_REG_STAT2] & 0x08U) == 0U) {
        return WIFI_BAT_HIGH;
    }

    /* TOP_OFF仍属于充电，同时确认充电使能没有关闭。 */
    if ((charge_status == 1U || charge_status == 2U ||
         charge_status == 3U) &&
        (g_sc89601_snapshot.reg[SC89601_REG_CTL1] & 0x10U) != 0U) {
        switch (level) {
        case WIFI_BAT_HIGH:
            return WIFI_BAT_CHARGE_H;
        case WIFI_BAT_MIDDLE:
            return WIFI_BAT_CHARGE_M;
        default:
            return WIFI_BAT_CHARGE_L;
        }
    }

    return level;
}

/* 观察最终显示值，使插拔电、充满和重新补电也能触发上报。 */
static void App_Wifi_BatObserve(void)
{
    u8 current_soc;

    current_soc = App_Wifi_GetBatDpValue();
    if (current_soc == WIFI_BAT_INVALID) {
        return;
    }

    if (s_wifi_test_phase == WIFI_TEST_BOOT ||
        s_wifi_bat_observed == 0xFFU) {
        s_wifi_bat_observed = current_soc;
        s_wifi_bat_cooldown_s =
            (current_soc == WIFI_BAT_LOW) ?
            WIFI_BAT_REPORT_INTERVAL_S : 0U;
    } else if (current_soc != s_wifi_bat_observed) {
        s_wifi_bat_observed = current_soc;
        s_wifi_bat_pending = 1U;
        g_wifi_allow_sleep = 0U;
    }

    /* 普通Low保留重复提醒，charge_L不参与。 */
    if (current_soc != WIFI_BAT_LOW) {
        s_wifi_bat_cooldown_s = 0U;
    }

    /* BOOT和重试期间也刷新待发值，避免补发过期充电图标。 */
    if (s_wifi_bat_pending) {
        s_wifi_bat_value = current_soc;
    }
}

/* 发送前取得当前显示值，成功与失败统一交给ReportEnum记账。 */
static void App_Wifi_BatService(void)
{
    u8 current_soc;

    if (!s_wifi_bat_pending || s_wifi_bat_retry_s) {
        return;
    }
    if (!App_Wifi_CanSendDp()) {
        return;
    }

    current_soc = App_Wifi_GetBatDpValue();
    if (current_soc == WIFI_BAT_INVALID) {
        return;
    }

    if (App_Wifi_ReportEnum(DPID_BAT, current_soc) != SUCCESS) {
        s_wifi_bat_retry_s = 1U;
    }
}

/* 温度事件回差锁存：>38℃触发，<=37℃持续确认后恢复。 */
static void App_Wifi_TempObserve(void)
{
    u8 current_temp;
    u8 need_report;

    current_temp = HAL_ADC_Get_Temperature_C();
    need_report = 0U;

    if (s_wifi_temp_high_observed == 0xFFU) {
        s_wifi_temp_high_observed =
            (current_temp > WIFI_TEMP_HIGH_C) ? 1U : 0U;
        s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;

        /* BOOT首报沿用启动/全量同步，其他阶段保留首次同步。 */
        if (s_wifi_test_phase != WIFI_TEST_BOOT) {
            need_report = 1U;
        }
    } else if (s_wifi_temp_high_observed == 0U) {
        if (current_temp > WIFI_TEMP_HIGH_C) {
            /* 首次越界立即触发；锁存后避免38/39反复触发。 */
            s_wifi_temp_high_observed = 1U;
            s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;
            need_report = 1U;
        }
    } else {
        if (current_temp > WIFI_TEMP_RECOVER_C) {
            /* 38℃仍保持锁存；回升时取消尚未完成的恢复确认。 */
            s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;
        } else if (s_wifi_temp_recover_wait_s ==
                   WIFI_TEMP_RECOVER_IDLE) {
            /* 多留一个秒边界，保证约10~11秒后才确认恢复。 */
            s_wifi_temp_recover_wait_s =
                WIFI_TEMP_RECOVER_CONFIRM_S + 1U;
        } else if (s_wifi_temp_recover_wait_s == 0U) {
            /* 持续<=37℃后解除锁存，允许下一次高温事件。 */
            s_wifi_temp_high_observed = 0U;
            s_wifi_temp_recover_wait_s = WIFI_TEMP_RECOVER_IDLE;
            need_report = 1U;
        }
    }

    /* 普通同步过滤1℃抖动，高温事件仍由上面的判断触发。 */
    if (s_wifi_test_phase != WIFI_TEST_BOOT &&
        s_wifi_temp_sync_due) {
        s_wifi_temp_sync_due = 0U;

        if (!s_wifi_temp_sent_valid ||
            ((current_temp >= s_wifi_temp_last_sent) ?
             (current_temp - s_wifi_temp_last_sent) :
             (s_wifi_temp_last_sent - current_temp)) >= 2U) {
            need_report = 1U;
        }
    }

    if (need_report) {
        s_wifi_temp_pending = 1U;
        g_wifi_allow_sleep = 0U;
    }

    /* 待发期间保留最新实际温度，沿用原有失败重试。 */
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

    result = App_Wifi_TempDpUpdate(s_wifi_temp_value);

		if (App_Wifi_DpTxSucceeded(result)){
        s_wifi_temp_last_sent = s_wifi_temp_value;
        s_wifi_temp_sent_valid = 1;
        s_wifi_temp_pending = 0;
    } else {
        s_wifi_temp_retry_s = 1;
    }
}

/* 水位发生改变时，重新开始连续确认，不累计变化次数。 */
static void App_Wifi_WaterObserve(void)
{
    u8 current_empty;

    current_empty = g_Device.is_empty ? 1U : 0U;

    if (s_wifi_water_observed != current_empty) {
        s_wifi_water_observed = current_empty;
        s_wifi_water_stable_s = 0;
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

    if (App_Wifi_DpTxSucceeded(result)) {
        s_wifi_water_pending = 0;

        g_Device_Shadow.is_empty =
            (s_wifi_water_value == 0U) ? 1U : 0U;
        /* 本地发送完成，不重新开启周期性缺水上报。 */
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

		if (App_Wifi_DpTxSucceeded(result)){
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

		if (App_Wifi_DpTxSucceeded(result)){
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

		if (App_Wifi_DpTxSucceeded(result)){
        s_wifi_filter_days_pending = 0;
    } else {
        s_wifi_filter_days_retry_s = 1;
    }
}

/* 处理用户重新配网请求；返回1表示本轮尝试发送过重置帧。 */
static u8 App_Wifi_ResetService(void)
{
    if (!g_Device.req_wifi_reset) {
        return 0;
    }

    if (s_wifi_test_phase != WIFI_TEST_BOOT &&
        s_wifi_test_phase != WIFI_TEST_ACTIVE) {
        return 0; /* 恢复期间保留请求，握手完成后再处理。 */
    }

    if (P21 != 0 ||
        stop_update_flag == ENABLE ||
        !WiFi_UART_IsTxReady()) {
        return 0; /* 暂不具备发送条件，不消耗配网请求。 */
    }

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
        g_Device.req_wifi_reset = 1; /* 发送失败，保留请求供恢复后重试。 */
        s_wifi_test_error = 3;
        s_wifi_test_error_printed = 0;
        s_wifi_test_phase = WIFI_TEST_ERROR; /* 交给异常阶段推进 CEN 恢复。 */
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
//        printf("\r\n[WPAIR] connected\r\n");
    } else if (s_wifi_pairing_hold_s == 0) {
//        printf("\r\n[WPAIR] timeout\r\n");
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

		if (App_Wifi_DpTxSucceeded(result)){
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

		if (!App_Wifi_CanSendDp()) {
				return;
		}

    result = mcu_dp_bool_update(DPID_HEAT, s_wifi_heat_value);

		if (App_Wifi_DpTxSucceeded(result)){
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

		if (App_Wifi_DpTxSucceeded(result)){
        s_wifi_uv_pending = 0;
        g_Device_Shadow.uvc_armed = s_wifi_uv_value;
    } else {
        s_wifi_uv_retry_s = 1;
    }
}

#define WIFI_TIME_RETRY_INTERVAL_S  60U
#define WIFI_TIME_TX_RETRY_S         3U

/* 合并主动校时请求，重复提交不打断已有的重试间隔。 */
void App_Wifi_TimeSyncSubmit(void)
{
    if (s_wifi_time_retry_s != 0U) {
        return; /* 修复：避免重复提交绕过重试间隔。 */
    }

    s_wifi_time_pending = 1;
    g_wifi_allow_sleep = 0;
}

/* BTM公共秒任务调用；只推进计时和提交待办，不执行UART发送。 */
void App_Wifi_TimeSyncTick1s(void)
{
    u8 retry_due = 0;
    u8 wifi_state;

    if (s_wifi_time_retry_s > 0U) {
        s_wifi_time_retry_s--;
        if (s_wifi_time_retry_s == 0U) {
            retry_due = 1;
        }
    }

    if (g_Device.current_unix_time != 0UL) {
        return; /* 已取得有效时间，结束首次校时的自动重试。 */
    }

    wifi_state = mcu_get_wifi_work_state();

    if (wifi_state == WIFI_CONN_CLOUD ||
        (retry_due && wifi_state == WIFI_LOW_POWER)) {
        App_Wifi_TimeSyncSubmit(); /* 到期后通过原有待发门控唤醒模组。 */
    }
}

/* 仅在合法时间响应已写入current_unix_time后调用。 */
void App_Wifi_TimeSyncDone(void)
{
    s_wifi_time_pending = 0;
    s_wifi_time_retry_s = 0; /* 修复：取得有效时间后清除未完成的重试。 */
}

/* 请求发出后释放通信待办；未取得时间时由公共秒任务继续补取。 */
static void App_Wifi_TimeSyncService(void)
{
    if (!s_wifi_time_pending || s_wifi_time_retry_s) {
        return;
    }

    if (!App_Wifi_CanSendDp()) {
        return;
    }

    mcu_get_green_time();

    if (wifi_uart_last_frame_tx_ok()) {
        s_wifi_time_pending = 0;
        s_wifi_time_retry_s = WIFI_TIME_RETRY_INTERVAL_S;
        /* 这里只确认请求已发出；没有有效时间时，到期仍会重新提交。 */
    } else {
        s_wifi_time_retry_s = WIFI_TIME_TX_RETRY_S;
        /* 保留待发请求，交给原有UART恢复流程处理。 */
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

		if (!App_Wifi_CanSendDp()) {
				return;
		}

    send_len = 0;
    send_len = set_wifi_uart_byte(send_len, 0x04);

    wifi_uart_write_frame(
        MODULE_EXTEND_FUN_CMD, MCU_TX_VER, send_len);

    tx_ok = wifi_uart_last_frame_tx_ok() ? 1 : 0;
    s_wifi_reset_notify_retry_s = tx_ok ? 5 : 1;

//    printf("\r\n[WREG] tx=%u\r\n",
//           (unsigned int)tx_ok);
}

void App_WifiTest_Task(void)
{
		/* 主动低电停机独立于通信故障恢复，避免CEN被自动重新拉高。 */
	if (App_FSM_BatteryShutdownActive()) {
			if (s_wifi_test_phase != WIFI_TEST_BAT_OFF ||
					!WiFi_CEN_IsHeld()) {
					WiFi_CEN_Assert();
					WiFi_Module_Release();
					wifi_protocol_init(); /* 清掉关闭前的半帧和旧联网状态。 */

					g_wifi_allow_sleep = 0U;
					g_wifi_lp_state = WIFI_LP_UNKNOWN;
					s_wifi_test_phase = WIFI_TEST_BAT_OFF;
					s_wifi_test_seconds = 0U;
			}
			return;
	}

	if (App_FSM_BatteryShutdownSyncActive()) {
			if (s_wifi_switch_pending &&
					s_wifi_test_phase < WIFI_TEST_ERROR &&
					!WiFi_CEN_IsHeld() &&
					mcu_get_wifi_work_state() == WIFI_CONN_CLOUD) {
					s_wifi_switch_value = 0U;
					WiFi_Module_KeepAwake();
					App_WifiTest_RunState();
					App_Wifi_SwitchService();
			}
			return;
	}

	if (s_wifi_test_phase == WIFI_TEST_BAT_OFF) {
			/* 插电解除保护后，重新同步当前状态，不续发旧操作。 */
			App_WifiTest_Init();
			App_Wifi_CenStart(WIFI_TEST_CEN_LOW);
	}

	App_Wifi_SwitchObserve();
	App_Wifi_ModeObserve();
	App_Wifi_BatObserve();
	App_Wifi_HeatObserve();
	App_Wifi_UvObserve();
	App_Wifi_TempObserve();
	App_Wifi_WaterObserve();
	App_Wifi_TdsObserve();

	App_WifiTest_RunState(); /* 先推进恢复，再决定是否发送。 */

	if (s_wifi_test_phase >= WIFI_TEST_ERROR) {
			return; /* 保留本地变化，暂停主动通信调度。 */
	}

	App_Wifi_PairingTask();

		/* 联网后补交注册需求，不依赖主动查询状态的回调。 */
		if (mcu_get_wifi_work_state() == WIFI_CONN_CLOUD &&
				!g_reset_notify_enabled) {
				App_Wifi_ResetNotifySubmit();
		}

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
	/* 主动关闭模组后，旧待发位和UART未就绪不再阻塞MCU休眠。 */
if (s_wifi_test_phase == WIFI_TEST_BAT_OFF) {
    return (App_FSM_BatteryShutdownActive() &&
            WiFi_CEN_IsHeld()) ? 1U : 0U;
}

    /*
     * 注册、校时已包含在ReportPending中，统一检查以减少重复代码；
     * 任意通信待办仍会阻止MCU进入STOP。
     */
    if (s_wifi_test_phase != WIFI_TEST_SLEEP ||
        s_wifi_pairing_hold ||
        g_Device.req_wifi_reset ||
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