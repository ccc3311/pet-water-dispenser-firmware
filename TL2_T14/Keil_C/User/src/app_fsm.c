#include "app_fsm.h"
#include "hal_adc.h"
#include "hal_pwm.h"
#include "hal_tm1650.h"
#include "hal_buzzer.h"
#include "hal_touch.h"
#include "hal_ir.h"
#include "wifi.h"
#include "hal_eeprom.h"
#include "app_wifi.h"
#include "hal_power.h"
#include "sc89601.h"

// 实例化全局数据总线与影子寄存器
volatile DeviceStatus_t g_Device;
volatile DeviceStatus_t g_Device_Shadow; 
volatile uint8_t g_cloud_data_synced = 0; // 0:未同步云端历史值, 1:已同步

// 【新增】：缺水灯极速响应标志 (脱离2秒防误杀束缚，与硬件断电同频)
uint8_t g_ui_water_empty = 0;
uint8_t typec_present;
// ==========================================
// 外部变量与宏定义
// ==========================================
#define BAT_PIN  P03    // 现用于检测充电 IC 的状态 (充电中/充满)

#define APP_CONSUMABLE_COUNTDOWN_ENABLE  0

#define APP_BAT_SHUTDOWN_SYNC_S  8U

extern int8_t    cached_temperature_c;    // ADC计算得出的当前水温
extern uint16_t  cached_sys_voltage_mv;   // ADC计算得出的系统电压

extern void HAL_Buzzer_Beep_3s(void);
// 【注意】：请确保 hal_ir.h/c 中提供了重置消抖缓存的函数，若无请自己补充一个清零计数的简单函数
extern void HAL_IR_Reset(void); 

extern void App_Cloud_Sync_Reset(void);
// ==========================================
// 1秒级子任务前置声明 (仅限本文件内部调用)
// ==========================================
static void Task_Basic_Countdown(void);
static void Task_Consumable_Life(void);
static void Task_Wifi_Pairing_Check(void);


#define APP_BAT_PUMP_STOP_MV       3500U
#define APP_BAT_PUMP_CONFIRM_TICKS 30U
#define APP_BAT_PUMP_UNKNOWN       0U
#define APP_BAT_PUMP_ALLOWED       1U
#define APP_BAT_PUMP_LOCKED        2U

#define APP_BAT_SHUTDOWN_MV       3350U
#define APP_BAT_SHUTDOWN_TICKS    25U
#define APP_BAT_SHUTDOWN_DELAY_S  300U
#define APP_BAT_PUMP_SHUTDOWN     3U

extern volatile sc_u8 g_sc89601_init_result;
extern volatile sc_u8 g_sc89601_poll_result;
extern volatile sc89601_snapshot_t g_sc89601_snapshot;

#define APP_CHARGE_TOP_OFF_ACTIVE  0x08U
#define APP_CHARGE_FAULT_MASK      0xFCU

static uint8_t s_charge_state = APP_CHARGE_UNKNOWN;

/* 300秒超过8位范围；软件关机不清除此计时。 */
static uint16_t s_bat_shutdown_left_s = 0;
/* 独立于App_FSM_Init，软件关机不会解除低压锁存。 */
static uint8_t s_bat_pump_state = APP_BAT_PUMP_UNKNOWN;
static uint8_t s_bat_pump_low_ticks = 0;

/* 每100ms更新停泵保护；锁存与保护关机均由确认插电解除。 */
static void Task_Battery_PumpProtect(uint8_t typec_present)
{
    uint16_t voltage_mv;

    if (typec_present && g_Device.power_mode == POWER_AC) {
        s_bat_pump_state = APP_BAT_PUMP_ALLOWED;
        s_bat_pump_low_ticks = 0;
        s_bat_shutdown_left_s = 0; /* 插电解除保护，同时取消关机计时。 */
        return;
    }

    if (s_bat_pump_state >= APP_BAT_PUMP_LOCKED) {
        return; /* 电压回弹不能解除锁存或保护关机。 */
    }

    voltage_mv = g_Device.sys_voltage_mv;
    if (voltage_mv == 0U) {
        s_bat_pump_low_ticks = 0;
        return;
    }

    if (s_bat_pump_state == APP_BAT_PUMP_UNKNOWN) {
        s_bat_pump_state =
            (voltage_mv < APP_BAT_PUMP_STOP_MV) ?
            APP_BAT_PUMP_LOCKED : APP_BAT_PUMP_ALLOWED;

        if (s_bat_pump_state == APP_BAT_PUMP_LOCKED) {
            s_bat_shutdown_left_s = APP_BAT_SHUTDOWN_DELAY_S + APP_BAT_SHUTDOWN_SYNC_S;
            /* 低压启动直接锁泵，并开始关机计时。 */
        }
        return;
    }

    if (voltage_mv >= APP_BAT_PUMP_STOP_MV) {
        s_bat_pump_low_ticks = 0;
        return;
    }

    if (s_bat_pump_low_ticks < APP_BAT_PUMP_CONFIRM_TICKS) {
        s_bat_pump_low_ticks++;
    }

    if (s_bat_pump_low_ticks >= APP_BAT_PUMP_CONFIRM_TICKS) {
        s_bat_pump_state = APP_BAT_PUMP_LOCKED;
        s_bat_shutdown_left_s = APP_BAT_SHUTDOWN_DELAY_S + APP_BAT_SHUTDOWN_SYNC_S;
        /* 仅在首次进入锁存时开始计时，后续不重复装载。 */
    }
}

/* 低电保护侧的供泵许可，实际运行仍受其他业务条件限制。 */
uint8_t App_FSM_BatteryPumpAllowed(void)
{
    return (s_bat_pump_state == APP_BAT_PUMP_ALLOWED) ? 1U : 0U;
}

/* 已确认锁泵或保护关机才返回1，未完成首次采样不算低电保护。 */
uint8_t App_FSM_BatteryProtectActive(void)
{
    return (s_bat_pump_state >= APP_BAT_PUMP_LOCKED) ? 1U : 0U;
}

/* 查询是否已进入保护关机，供供电、UI和休眠逻辑使用。 */
uint8_t App_FSM_BatteryShutdownActive(void)
{
    return (s_bat_pump_state == APP_BAT_PUMP_SHUTDOWN) ? 1U : 0U;
}

/* 主循环按BTM秒节拍调用，软件关机和休眠期间仍继续计时。 */
void App_FSM_BatteryTick_1s(void)
{
    if ((s_bat_pump_state == APP_BAT_PUMP_LOCKED) &&
        (s_bat_shutdown_left_s > 0U)) {
        s_bat_shutdown_left_s--;

				if (s_bat_shutdown_left_s == APP_BAT_SHUTDOWN_SYNC_S) {
						g_Device.state = STATE_OFF;
						g_Device.req_power_toggle = 0U;
						App_Wifi_SwitchSubmit(0U);
				} else if (s_bat_shutdown_left_s == 0U) {
						s_bat_pump_state = APP_BAT_PUMP_SHUTDOWN;
				}
    }
}

uint8_t App_FSM_BatteryShutdownSyncActive(void)
{
    return (s_bat_pump_state == APP_BAT_PUMP_LOCKED &&
            s_bat_shutdown_left_s <= APP_BAT_SHUTDOWN_SYNC_S) ? 1U : 0U;
}
// ==========================================
// 状态机初始化 (开机绝对安全态)
// ==========================================
void App_FSM_Init(void) {
    g_Device.state = STATE_OFF; 
    
    g_Device.ir_triggered = 0;
    
    g_Device.req_power_toggle = 0;
    g_Device.req_mode_toggle = 0;
    g_Device.req_heating = 0;
    g_Device.req_uvc = 0;
    
    g_Device.timer_pump_s = 0;
    g_Device.timer_uvc_s = 0;
    g_Device.display_timer_s = 0; 
    
    g_Device.heating_enabled = 0; 
    g_Device.uvc_armed = 0;       
    
    // 2. 【核心修复】：在加载 EEPROM 之后，强制清零那些绝不允许被 EEPROM 污染的运行状态！
    // 防止结构体变更导致的脏数据残留
    g_Device.wifi_pairing_timer_s = 0; 
    g_Device.req_wifi_reset = 0;
		g_Device.req_cancel_pairing = 0;//WIFI重置命令
		
		
		//滤芯报警功能
		HAL_EEPROM_Load_AllData(); // 【统一读取】
		
		/* 保留合法的 0 天，仅对未初始化值使用默认周期。 */
		if (g_Device.target_clean_days == 0xFFFFU) {
				g_Device.target_clean_days = DEFAULT_CLEAN_DAYS;
		}

		if (g_Device.target_filter_days == 0xFFFFU) {
				g_Device.target_filter_days = DEFAULT_FILTER_DAYS;
		}
		// ==========================================
    // 【核心修复】：防止新机器/未配网机器 EEPROM 读出 0xFF，导致默认静音
    // 只有在 APP 明确下发 1 的时候才是静音，其他乱码(0xFF)全强制重置为 0(发声)
    // ==========================================
    if (g_Device.mute_enabled != 1) {
        g_Device.mute_enabled = 0; 
    }
}

// ==========================================
// 秒级时间片引擎 (由 main.c 每 1000ms 调用)
// ==========================================
void App_FSM_Tick_1s(void) {
    // 1. 基础倒计时与时间戳推进
    Task_Basic_Countdown();
    
    // 3. 滤芯与清洗寿命推演
    Task_Consumable_Life();
    
    // 4. 配网超时与状态监测
    Task_Wifi_Pairing_Check();
}

/* 出水请求由调用处判断，此处检查电池保护许可。 */
static void App_FSM_PumpStartIfReady(void)
{
    if (App_FSM_BatteryPumpAllowed()) { /* 无需等待OTG，保留低压停泵保护。 */
        HAL_PWM_Pump_Start();
    } else {
        HAL_PWM_Pump_Stop();
    }
}

// ------------------------------------------
// 子任务 1：基础硬件状态机倒计时
// ------------------------------------------
static void Task_Basic_Countdown(void) {

    // 1. 物理外设的工作倒计时推进
		if (g_Device.timer_pump_s > 0) { /* 不再等待升压状态，按秒推进出水倒计时。 */
				g_Device.timer_pump_s--;
		}
		/* 等待升压时保留出水时间；故障锁定后允许请求倒计时结束。 */
    if (g_Device.timer_uvc_s > 0)     g_Device.timer_uvc_s--;     // 倒数完毕后，UVC杀菌灯自动关闭
    if (g_Device.display_timer_s > 0) g_Device.display_timer_s--; // 倒数完毕后，UI指示灯自动息屏（省电）
    
    // 2. 本地 RTC 时间引擎自增 (充当系统的时钟跳动)
    // 只有在成功获取过云端时间后 (current_unix_time > 0)，本地才会开始推进时间
    if (g_Device.current_unix_time > 0) {
        g_Device.current_unix_time++; 
    }
}

// ------------------------------------------
// 子任务 3：耗材报警时长递减推演
// ------------------------------------------
/* 返回有效整天数；0表示不足一天或时间跨度无效。 */
#if APP_CONSUMABLE_COUNTDOWN_ENABLE
static uint16_t App_Consumable_DaysPassed(unsigned long now, unsigned long last)
{
    unsigned long days;

    /* 校时可能使时间倒退，先拦截，防止无符号减法回绕。 */
    if (now < last) {
        return 0;
    }

    /* 四处耗材计算共用此实现，减少重复代码；整数除法保留整天数。 */
    days = (now - last) / 86400UL;

    /* 有效结果为0～364，返回16位值即可供扣减和到期判断使用。 */
    return (uint16_t)days;
}

/* 分别结算清洗和滤芯，保留不足一天的余量。 */
static void Task_Consumable_Life(void)
{
    /* 共用计算已限制有效范围，临时天数由32位改为16位。 */
    uint16_t days_passed;

    if (g_Device.target_clean_days > 0 &&
        g_Device.current_unix_time > 0UL &&
        g_Device.last_clean_timestamp > 0UL) {

        /* 清洗使用自身时间基准，复用整天数计算和跳变过滤。 */
        days_passed = App_Consumable_DaysPassed(
            g_Device.current_unix_time,
            g_Device.last_clean_timestamp);

        /* 结果为0时统一跳过结算，代替原来的天数范围判断。 */
        if (days_passed != 0U) {
            if (days_passed >= g_Device.target_clean_days) {
                g_Device.target_clean_days = 0;
                g_Device.req_clean_alarm = 1;
            } else {
                g_Device.target_clean_days -= days_passed;
            }

            /*
             * 天数改为16位后，仍按32位秒数推进基准；
             * 只推进已经扣除的整天，保留不足一天的余量。
             */
            g_Device.last_clean_timestamp +=
                (unsigned long)days_passed * 86400UL;

            HAL_EEPROM_Save_AllData();
            App_Wifi_CleanDaysSubmit();
        }
    }

    if (g_Device.target_filter_days > 0 &&
        g_Device.current_unix_time > 0UL &&
        g_Device.last_filter_timestamp > 0UL) {

        /* 滤芯独立计算，使用自身基准，避免沿用清洗结果误扣天数。 */
        days_passed = App_Consumable_DaysPassed(
            g_Device.current_unix_time,
            g_Device.last_filter_timestamp);

        /* 共用函数已完成范围过滤，非0结果才执行原扣减流程。 */
        if (days_passed != 0U) {
            if (days_passed >= g_Device.target_filter_days) {
                g_Device.target_filter_days = 0;
                g_Device.req_filter_alarm = 1;
            } else {
                g_Device.target_filter_days -= days_passed;
            }

            /* 按32位秒数累计滤芯已结算时间，保留不足一天的余量。 */
            g_Device.last_filter_timestamp +=
                (unsigned long)days_passed * 86400UL;

            HAL_EEPROM_Save_AllData();
            App_Wifi_FilterDaysSubmit();
        }
    }
}

/* STOP巡检只判断是否需要结算，实际扣减仍由原秒任务执行。 */
uint8_t App_FSM_ConsumableUpdateDue(void)
{
    unsigned long now;

    now = g_Device.current_unix_time;
    if (now == 0UL) {
        return 0;
    }

    /*
     * 复用与扣减任务相同的计算和过滤规则，减少重复实现；
     * 返回1让主程序退出STOP，再由原秒任务扣减、保存和上报。
     */
    if (g_Device.target_clean_days > 0 &&
        g_Device.last_clean_timestamp > 0UL &&
        App_Consumable_DaysPassed(
            now, g_Device.last_clean_timestamp) != 0U) {
        return 1;
    }

    /* 滤芯使用独立基准复用同一规则，满一个有效整天后请求唤醒。 */
    if (g_Device.target_filter_days > 0 &&
        g_Device.last_filter_timestamp > 0UL &&
        App_Consumable_DaysPassed(
            now, g_Device.last_filter_timestamp) != 0U) {
        return 1;
    }

    return 0;
}
#else
/* 诊断版暂停耗材自动结算。 */
static void Task_Consumable_Life(void)
{
}

uint8_t App_FSM_ConsumableUpdateDue(void)
{
    return 0U;
}
#endif

void App_FSM_Consumable_Tick_1s(void)
{
    Task_Consumable_Life();
}

// ==========================================
// 子任务 4：WiFi 配网超时监测与状态兜底
// ==========================================
static void Task_Wifi_Pairing_Check(void) {
    uint8_t current_wifi_state = mcu_get_wifi_work_state();
    
    // 【核心修复】：新增断开连接等待标志，彻底告别硬延时盲等
    static uint8_t wait_for_disconnect = 0; 

    // 2. 拦截双击取消配网
    if (g_Device.user_cancelled_pairing) {
        if (current_wifi_state > 1 && current_wifi_state != 6) {
            g_Device.user_cancelled_pairing = 0;
        }
    } 

    // 【关键防呆】：只要触发了全新配网（被赋值为180），强制拦截后续的 3/4 状态判决
    if (g_Device.wifi_pairing_timer_s == 180) {
        wait_for_disconnect = 1;
    }

    // 3. 倒计时执行与判定
    if (g_Device.wifi_pairing_timer_s > 0) {
        g_Device.wifi_pairing_timer_s--; 

        // 【状态机推进】：只要模组状态变成了非连接态（配网中0/1/6，或断开2/5），
        // 说明模组重启已完成且底层缓存状态已被最新串口包刷新，解除拦截。
        if (current_wifi_state <= 2 || current_wifi_state == 5 || current_wifi_state == 6) {
            wait_for_disconnect = 0;
        }

        // 状态 3：已连上路由器； 状态 4：已连上路由器且连接到云端
        if (current_wifi_state == 3 || current_wifi_state == 4) {
            // 【致命修复】：必须明确看到模组先断开旧连接（wait_for_disconnect == 0）后，
            // 再次连上网，才算是这一次的配网真正成功！
            if (wait_for_disconnect == 0) {
                g_Device.wifi_pairing_timer_s = 0;  
                HAL_Buzzer_Beep(1); // 连云成功提示音
            }
        }
        // 如果倒计时自然流逝归 0，说明配网超时或被用户主动取消
        else if (g_Device.wifi_pairing_timer_s == 0) {
            HAL_Buzzer_Beep(2); // 失败/超时提示音
        }
    } else {
        // 倒计时非工作期间，保证标志位复位清零
        wait_for_disconnect = 0;
    }
}


void App_FSM_Pairing_Tick_1s(void)
{
    Task_Wifi_Pairing_Check();
}


// 定义每个月的天数（平年）
const unsigned char code month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/**
 * @brief 将YY-MM-DD HH:MM:SS转换为相对2000年的总秒数
 * 注意：51单片机常数运算必须加 'ul' 后缀防止16位溢出！
 */
/* 将日期转换为相对2000年的秒数，沿用原接口和时间基准。 */
unsigned long Convert_To_Relative_Sec(
    unsigned char year_since_2000,
    unsigned char month, unsigned char day,
    unsigned char hour, unsigned char minute, unsigned char second)
{
    unsigned long days;
    unsigned char leap_years;
    unsigned char i;

    /* 直接计算此前整年的闰年数，代替原来的逐年累计循环。 */
    leap_years =
        (unsigned char)((year_since_2000 + 3U) / 4U);

    /*
     * 8位年份覆盖2000～2255，2100、2200为平年。
     * 严格大于100/200时，才扣除已经结束的对应世纪年。
     */
    if (year_since_2000 > 100U) {
        leap_years--;
    }
    if (year_since_2000 > 200U) {
        leap_years--;
    }

    /* 按32位计算整年天数，避免年份乘365超出16位范围。 */
    days =
        (unsigned long)year_since_2000 * 365UL + leap_years;

    for (i = 0; i < month - 1; i++) {
        days += month_days[i];
    }

    /*
     * 当前年份的2月补偿独立处理：
     * 在现有年份范围内，直接排除2100、2200，替代世纪取模判断。
     */
    if (month > 2U &&
        (year_since_2000 & 3U) == 0U &&
        year_since_2000 != 100U &&
        year_since_2000 != 200U) {
        days++;
    }

    days += (day - 1);

    return (days * 86400UL) +
           ((unsigned long)hour * 3600UL) +
           ((unsigned long)minute * 60UL) + second;
}

/* 每100ms归纳充电状态，本地显示与后续APP上报共用。 */
static void Task_ChargeState(uint8_t typec_present)
{
    if (!typec_present) {
        s_charge_state = APP_CHARGE_NONE;
        return; /* 拔电清除本次充电完成状态。 */
    }

    s_charge_state = APP_CHARGE_UNKNOWN;

    if ((g_Device.power_mode != POWER_AC) ||
        (g_sc89601_init_result != SC89601_OK) ||
        (g_sc89601_poll_result != SC89601_OK) ||
        ((g_sc89601_snapshot.fault_current &
          APP_CHARGE_FAULT_MASK) != 0U)) {
        return; /* 供电待确认、快照无效或主要故障时不报告完成。 */
    }

    switch (g_sc89601_snapshot.charge_status) {
        case 0U:
            s_charge_state = APP_CHARGE_NONE;
            break;

        case 1U:
        case 2U:
            s_charge_state = APP_CHARGE_ACTIVE;
            break;

        case 3U:
            s_charge_state =
                (g_sc89601_snapshot.reg[SC89601_REG_STAT2] &
                 APP_CHARGE_TOP_OFF_ACTIVE) ?
                APP_CHARGE_ACTIVE : APP_CHARGE_DONE;
            break; /* 补充充电进行期间仍归为充电中。 */

        default:
            break; /* 保持未确认。 */
    }
}

/* 读取统一状态，不触发额外I2C访问。 */
uint8_t App_FSM_GetChargeState(void)
{
    return s_charge_state;
}

// ==========================================
// 核心业务大脑 (剥离了所有的 UI 渲染，只做逻辑与外设执行)
// ==========================================
void App_FSM_Run(void) {
    static uint8_t uvc_cooling_flag = 0; 
    static uint8_t bat_debounce = 0;    
   
		// 【新增】：用于液位恢复的慢速释放计数器
		static uint8_t empty_stable_cnt = 0; // 缺水触发计时器
    static uint8_t full_stable_cnt = 0;  // 满水恢复计时器
	    static uint8_t water_stable_full = 0; // 【新增】：外设开启的物理允许标志 (0:禁止开启, 1:允许开启)
    uint8_t hardware_empty;
	
		// ==========================================
    // 【新增】：P02与TM1650的滤波与状态控制变量
    // ==========================================
    static uint8_t p02_low_cnt = 0;
    static uint8_t p02_high_cnt = 0;
    
    static uint8_t tm1650_init_done = 0; // TM1650 单次初始化锁 (0:未初始化, 1:已完成)
    static uint8_t tm1650_delay_cnt = 0; // TM1650 1秒延时计数器
	
    // ==========================================
    // 1. 传感器感知层 (更新数据总线)
    // ==========================================
    g_Device.sys_voltage_mv = HAL_ADC_Get_SystemVoltage_mV();
    g_Device.water_quality  = HAL_ADC_Get_WaterQuality();
		/* 本轮供电与显示初始化共用同一个插电状态。 */
		typec_present = HAL_Power_TypeCIsPresent();
		// ==========================================
    // 【新增】：P02 供电控制与双向滤波 (每 100ms 进来一次)解决低电量无法充电问题
    // ==========================================
			if (typec_present) {
					p02_low_cnt = 0;
					p02_high_cnt = 0;

					if (P00 == 0) {
							P00 = 1; /* 插电后恢复外设供电，不等待电池电压回升。 */
							tm1650_init_done = 0; /* 重新上电后重新初始化显示。 */
							tm1650_delay_cnt = 0;

							App_Cloud_Sync_Reset(); /* 保留原有P00上电后的同步处理。 */
					}
			}
			else if (App_FSM_BatteryShutdownActive()) {
					p02_low_cnt = 0;
					p02_high_cnt = 0;

					if (P00 != 0) {
							HAL_TM1650_PowerOff();
							P00 = 0; /* 保护关机后持续断供，禁止回弹重新打开。 */
					}
			}
			else if ((g_Device.sys_voltage_mv != 0U) &&
							 (g_Device.sys_voltage_mv < APP_BAT_SHUTDOWN_MV)) {
					p02_high_cnt = 0;

					if (p02_low_cnt < APP_BAT_SHUTDOWN_TICKS) {
							p02_low_cnt++;
					}

					if (p02_low_cnt >= APP_BAT_SHUTDOWN_TICKS) {
							s_bat_pump_state = APP_BAT_PUMP_SHUTDOWN;
							s_bat_shutdown_left_s = 0; /* 严重低压优先，提前结束提示期。 */

							if (P00 != 0) {
									HAL_TM1650_PowerOff();
									P00 = 0;
							}
					}
			} 
    else if (g_Device.sys_voltage_mv > 3400) {
        p02_low_cnt = 0; // 只要电压回升，立刻打断低压断电计时
        if (p02_high_cnt < 25) {
            p02_high_cnt++;
        } else {
            if (P00 == 0) { // 增加边沿锁存，确保只在恢复供电的瞬间执行一次
                P00 = 1; 
                App_Cloud_Sync_Reset();// WIFI模组刚刚被重新上电，强制 MCU 清除所有云端同步的旧记忆
							
//							// 【新增】：外设恢复供电提示音 (实际发声 800ms)
//                HAL_Buzzer_Beep_3s(); 
            }
        }
    } 
    else {
        // [迟滞死区]：电压落在 3350 ~ 3400 之间。
        // 清零所有计时器，什么都不做，死死维持 P02 当前的状态。					

        p02_low_cnt = 0;
        p02_high_cnt = 0;
    }
		
		// --- 2. TM1650 延时单次初始化 (绝对依赖 P02 物理供电) ---
			if (P00 == 0) {
					tm1650_init_done = 0;
					tm1650_delay_cnt = 0;
			}
			else if (typec_present ||
							 g_Device.sys_voltage_mv > 3400U) {
					/* 插电时允许低电池状态初始化显示。 */
					if (tm1650_init_done == 0) {
							if (tm1650_delay_cnt <
									(typec_present ? 2U : 10U)) {
									/* 外电等待约200ms；电池恢复沿用约1秒。 */
									tm1650_delay_cnt++;
							} else {
									HAL_TM1650_Init();
									tm1650_init_done = 1;
							}
					}
			}
			else {
					tm1650_delay_cnt = 0;
			}
		
	// 【核心重构】：不对称消抖处理液位跳动
    hardware_empty = HAL_ADC_Get_IsEmpty();
		
    // 【核心重构：双向消抖】
    if (hardware_empty) {
        // --- 物理层正在缺水或晃动 ---
			        water_stable_full = 0; // 【核心修改】：只要检测到缺水(哪怕一瞬间)，立刻剥夺外设开启权限！
        full_stable_cnt = 0; // 只要晃一下，立刻打断“满水恢复”，死死维持缺水态！（实现你的想法）
        
        if (empty_stable_cnt < 10) {
            empty_stable_cnt++;
        } else {
            g_Device.is_empty = 1; // 连续 2 秒真缺水（不是小波纹），才正式通知全系统
        }
    } else {
        // --- 物理层正在满水 ---
        empty_stable_cnt = 0; // 只要有水，立刻打断“缺水触发”，保护标志位不被误杀！
        
        if (full_stable_cnt < 10) {
            full_stable_cnt++;
        } else {
					            water_stable_full = 1; // 【核心修改】：连续2秒毫无波纹，才真正允许外设启动
            g_Device.is_empty = 0; // 连续 2 秒毫无波纹，才允许水泵恢复
        }
    }
		
		    // ==========================================
    // 【新增】：缺水灯极速响应与锁存逻辑
    // ==========================================
    // 1. 只要物理层检测到缺水（或水面晃动），立刻点亮缺水灯（无延迟）
    if (hardware_empty) {
        g_ui_water_empty = 1; 
    } 
    // 2. 只有当水面连续稳定高水位（连续2秒毫无波纹）时，才熄灭缺水灯
    else if (water_stable_full) {
        g_ui_water_empty = 0;
    }
		
    // 供电判定由 TYPC_ADC 实际输入电压决定
		if (!HAL_Power_TypeCIsPresent()) {
				if (bat_debounce < 5) bat_debounce++;
    } else {
        if (bat_debounce > 0) bat_debounce--;
    }

    if (bat_debounce >= 3) {
        g_Device.power_mode = POWER_BATTERY;
        // 电池模式下强制剥夺大功率外设权限
        g_Device.heating_enabled = 0;
        g_Device.uvc_armed = 0;
        g_Device.timer_uvc_s = 0; 
			
			// 【核心修复】：强制同步目标模式，修正 UI 渲染，并唤醒屏幕显示
        if (g_Device.target_work_mode != 0) {
            g_Device.target_work_mode = 0; 
            g_Device.display_timer_s = 10; // 唤醒屏幕10秒，直观提示用户已降级到感应模式
        }
				
        if (g_Device.state == STATE_CONTINUOUS) {
            // 【丝滑修复】：断电降级时，同样注入延时平滑过渡
            g_Device.state = STATE_SENSING;
            g_Device.timer_pump_s = 8; 
        }
    } else {
        g_Device.power_mode = POWER_AC;
    }
		/* 使用已经确认的供电模式更新停泵保护。 */
		Task_Battery_PumpProtect(typec_present);
		Task_ChargeState(typec_present); /* 供电模式更新后归纳充电状态。 */
		
if (App_FSM_BatteryShutdownActive() ||
    App_FSM_BatteryShutdownSyncActive()) {
    g_Device.state = STATE_OFF;
    g_Device.target_work_mode = 0;
    g_Device.ir_triggered = 0;

    /* 丢弃操作请求，避免保护关机后重新开启。 */
    g_Device.req_power_toggle = 0;
    g_Device.req_mode_toggle = 0;
    g_Device.req_heating = 0;
    g_Device.req_uvc = 0;
    g_Device.req_wifi_reset = 0;
    g_Device.req_cancel_pairing = 0;

    g_Device.timer_pump_s = 0;
    g_Device.timer_uvc_s = 0;
    g_Device.display_timer_s = 0;
    g_Device.wifi_pairing_timer_s = 0;
    g_Device.heating_enabled = 0;
    g_Device.uvc_armed = 0;

    HAL_PWM_Pump_Stop();
    HAL_PWM_Heating_Stop();
    HAL_UVCING_Stop();
    return; /* 本轮不再执行后续业务。 */
		}
    // ==========================================
    // 拦截双击取消配网
    // ==========================================
    if (g_Device.req_cancel_pairing) {
        g_Device.req_cancel_pairing = 0;
        
        // 【核心修复】：标记为用户主动取消，防止被1秒任务兜底复活
        g_Device.user_cancelled_pairing = 1; 
        
			// 2. 【核心修复】：必须立刻清零配网倒计时，彻底释放对 UI 灯光的霸占！
        g_Device.wifi_pairing_timer_s = 0;
			
        // 【必须删除】：mcu_reset_wifi(); 
        // 原因：涂鸦SDK没有退出配网指令，发这个会让模组重新进入配网，导致APP一直显示可配网。
        // 让模组自己跑完内部的3分钟超时即可。
    }
		
    // ==========================================
    // 2. 最高指令 (处理长按开关机)
    // ==========================================
    if (g_Device.req_power_toggle) {
        g_Device.req_power_toggle = 0;
        HAL_Buzzer_Beep_3s(); 
        
        if (g_Device.state == STATE_OFF) {
            g_Device.state = STATE_IDLE; 
            g_Device.display_timer_s = 10; 
            g_Device.target_work_mode = 0; // 开机默认回到感应模式
        } else {
            App_FSM_Init(); // 关机重置内部状态
					            g_Device.target_work_mode = 0U;  // 关机后目标模式回到感应
        }
    }

		// ==========================================
    // 生死攸关 (缺水强制保护拦截)
    // ==========================================
    if (g_Device.state != STATE_OFF) {
        if (g_Device.is_empty) {
            if (g_Device.state != STATE_ERROR) {
                g_Device.state = STATE_ERROR;
                
                // 【满足你的诉求B】：发生了连续 2 秒的真缺水，果断清空所有大功率意图！
                g_Device.heating_enabled = 0;
                g_Device.uvc_armed = 0;
                g_Device.timer_uvc_s = 0;
                g_Device.timer_pump_s = 0;
            }
        } else if (g_Device.state == STATE_ERROR) {
            // 退出缺水态：水已经加满，并且水面连续 2 秒毫无波纹。
            // 此时加热和杀菌处于 0 状态，不会发生加水瞬间猛然加热的烫伤风险，必须等用户去APP或按键主动开启。
            
            // 根据记忆的目标模式和当前的供电环境，安全地恢复水泵运行：
            if (g_Device.target_work_mode == 1 && g_Device.power_mode == POWER_AC) {
                // 如果之前是持续出水，且插着电源，直接恢复长流水
                g_Device.state = STATE_CONTINUOUS;
                App_FSM_PumpStartIfReady(); 
            } else {
                // 否则（包括电池模式强制降级，或者本来就是感应模式），退回到待机感应态
                g_Device.state = STATE_IDLE;
                g_Device.target_work_mode = 0; 
            }
        }
    }
    // ==========================================
    // 4. 传感器联动逻辑 (红外唤醒)
    // ==========================================
    if (g_Device.state != STATE_OFF && g_Device.state != STATE_ERROR) {
        if (g_Device.ir_triggered) {
            g_Device.timer_pump_s = 12; //1. 维持原本的水泵感应工作 15s 因为红外5s锁定触发所以这里参数+7s//一体板红外3s，g_Device.timer_pump_s锁定改为12s
            if (g_Device.uvc_armed) {
                g_Device.timer_uvc_s = 590; // 2. 【核心修改】：如果在杀菌开启状态下感应到物体，点亮/重置杀菌灯 10 分钟，修改为590s实测，因为状态机的调度延迟时间会有延长误差
            }
        }
    }
/* 电池低电量时取消出水计时，避免红外触发或旧倒计时继续驱动水泵。 */
if (!App_FSM_BatteryPumpAllowed()) {
    g_Device.timer_pump_s = 0; /* 禁止旧计时或红外请求继续驱动水泵。 */
}
    // ==========================================
    // 5. 核心业务流转 (水泵控制)
    // ==========================================
    switch (g_Device.state) {
        case STATE_OFF:
        case STATE_ERROR:
            /* 关机或缺水时只切换模式意图，本分支不启动水泵。 */
            if (g_Device.req_mode_toggle) {
                g_Device.req_mode_toggle = 0;

                if (g_Device.target_work_mode == 0) {
                    if (g_Device.power_mode == POWER_AC) {
                        g_Device.target_work_mode = 1;
                    }
                } else {
                    g_Device.target_work_mode = 0;
                }

                /* 三条路径都提示一次，合并调用并保留原执行位置。 */
                HAL_Buzzer_Beep(1);
                g_Device.display_timer_s = 10;
            }
            break;

        case STATE_IDLE:
        case STATE_SENSING:
            // 纯依靠 timer_pump_s 维护运行
            if (g_Device.timer_pump_s > 0) {
                if (g_Device.state == STATE_IDLE) {
                    g_Device.state = STATE_SENSING;
                    App_FSM_PumpStartIfReady();
                }
            } else {
                if (g_Device.state == STATE_SENSING) {
                    g_Device.state = STATE_IDLE;
                    HAL_PWM_Pump_Stop();
                }
            }
            
            // 单击切换到持续模式
            if (g_Device.req_mode_toggle) {
                g_Device.req_mode_toggle = 0;
                HAL_Buzzer_Beep(1); 
                if (g_Device.power_mode == POWER_AC) {
                    g_Device.state = STATE_CONTINUOUS;
                    g_Device.target_work_mode = 1; 
                    App_FSM_PumpStartIfReady();
                } 
                g_Device.display_timer_s = 10; 
            }
            break;

        case STATE_CONTINUOUS:
            // 再次单击退出持续模式
            if (g_Device.req_mode_toggle) {
                g_Device.req_mode_toggle = 0;
                HAL_Buzzer_Beep(1);
                
                // 【丝滑修复】：无视当前倒计时，强制注入 8 秒延时，平滑切入感应态
                g_Device.state = STATE_SENSING; 
                g_Device.timer_pump_s = 8; // 核心：保持水泵运行，交由红外接管
                
                g_Device.target_work_mode = 0; // 【修复】：补全结构体前缀
                g_Device.display_timer_s = 10;
            }
            break;
    }

    // ==========================================
    // 6. 辅助按键控制逻辑 (加热与UVC)
    // ==========================================
    if (g_Device.state != STATE_OFF) {  
        if (g_Device.req_heating) {
            g_Device.req_heating = 0;
            if (g_Device.power_mode == POWER_AC && !g_Device.is_empty) {
                g_Device.heating_enabled = !g_Device.heating_enabled; 
            }
        }

        if (g_Device.req_uvc) {
            g_Device.req_uvc = 0;
            if (g_Device.power_mode == POWER_AC && !g_Device.is_empty) {
                HAL_Buzzer_Beep(1); 
                if (g_Device.uvc_armed) {
                    g_Device.uvc_armed = 0;    
                    g_Device.timer_uvc_s = 0;  
                } else {
                    g_Device.uvc_armed = 1;    
                }
            } else {
                HAL_Buzzer_Beep(1); 
            }
        }
    } else {
        g_Device.req_heating = 0;
        g_Device.req_uvc = 0;
    }

    // ==========================================
    // 7. 物理外设执行层 (完全由状态变量驱动)
    // ==========================================
    if (g_Device.state != STATE_OFF) {
        // UVC 执行
        if (g_Device.timer_uvc_s > 0 && water_stable_full && g_Device.is_empty == 0) {
            HAL_UVCING_Start();
            uvc_cooling_flag = 1; 
        } else {
            if (uvc_cooling_flag) {
                HAL_UVCING_Stop();
                uvc_cooling_flag = 0;
                // 【修改】：移除 g_Device.uvc_armed = 0; 
                // 倒计时结束只关闭物理紫外线灯，保持 uvc_armed = 1。
                // 这样 UI 指示灯会保持常亮，且下次红外触发时会再次点亮物理灯。
						}
        }

        // 加热执行 (带过温拦截)
        if (g_Device.heating_enabled && g_Device.power_mode == POWER_AC && !g_Device.is_empty && water_stable_full) {
            if (cached_temperature_c > 38) {
                HAL_PWM_Heating_Stop();
            } else {
                HAL_PWM_Heating_Start();
            }
        } else {
            HAL_PWM_Heating_Stop();
        }
        
				/* 过温、缺水或电池低电量时，强制停止水泵。 */
				if ((cached_temperature_c > 37) ||
						(hardware_empty != 0) ||
						!App_FSM_BatteryPumpAllowed()) { /* 停泵由保护许可决定。 */

						HAL_PWM_Pump_Stop();
				} else {
            /* 两种运行条件共用启泵调用，保留原短路判断顺序。 */
            if ((g_Device.state == STATE_CONTINUOUS) ||
                ((g_Device.state == STATE_SENSING) &&
                 (g_Device.timer_pump_s > 0))) {
                App_FSM_PumpStartIfReady();
            } else {
                HAL_PWM_Pump_Stop();
            }
        }
    } else {
        // 关机物理清场
        HAL_PWM_Pump_Stop();
        HAL_PWM_Heating_Stop();
        HAL_UVCING_Stop();
    }
}

static void App_UI_SetSenseIndicators(uint8_t status)
{
    /* 合并两处五灯刷新代码，保留原来的逐灯调用及顺序。 */
    HAL_LED_SetStatus(LED_SENSE_IND_1, status);
    HAL_LED_SetStatus(LED_SENSE_IND_2, status);
    HAL_LED_SetStatus(LED_SENSE_IND_3, status);
    HAL_LED_SetStatus(LED_SENSE_IND_4, status);
    HAL_LED_SetStatus(LED_SENSE_IND_5, status);
}

// ==========================================
// 独立 UI 渲染层：只读不写
// ==========================================
void App_UI_Refresh_Task(void) {
    static uint8_t blink_tick = 0;
    static uint8_t blink_slow = 0; 
    uint8_t ir_led_state;
		static uint8_t last_typec_present = 0;
		uint8_t typec_present;
	if (App_FSM_BatteryShutdownActive()) {
    return; /* P00已关闭，停止向显示芯片刷新。 */
		}
		typec_present = HAL_Power_TypeCIsPresent();

		if (typec_present && !last_typec_present) {
				blink_tick = 0;
				blink_slow = 1; /* 插电先亮，随后按原节奏慢闪。 */
		}

		last_typec_present = typec_present;
    // 100ms 轮询产生 800ms 慢闪节拍
    if (++blink_tick >= 8) {       
        blink_tick = 0;
        blink_slow = !blink_slow;
    }

		// ----------------------------------------------------
    // 4. Wi-Fi 配网状态覆写 
    // ----------------------------------------------------
    // 【核心修复2】：完全剥离对 mcu_get_wifi_work_state() 的实时依赖。
    // 只要我们的 3 分钟倒计时没结束（没超时也没被单击取消），就霸占 UI 强制闪烁！
		// 【核心修复】：增加 g_Device.state != STATE_OFF 判断。
    // 关机状态下云端解绑依旧允许模组进入配网，但单片机不闪烁指示灯。
    if (g_Device.wifi_pairing_timer_s > 0 && g_Device.state != STATE_OFF) {
        
        uint8_t net_led_status = blink_slow ? LED_ON : LED_OFF; 
        
        // 强行覆盖上述算好的业务状态，产生全盘闪烁效果
        HAL_LED_SetStatus(LED_NO_WATER,  net_led_status);
        HAL_LED_SetStatus(LED_WATER_Q,   net_led_status);
        HAL_LED_SetStatus(LED_HEATING,   net_led_status);
        HAL_LED_SetStatus(LED_MODE_CONT, net_led_status);
        HAL_LED_SetStatus(LED_MODE_SENSE,net_led_status);
        
        HAL_LED_SetStatus(LED_SW2, net_led_status);
        HAL_LED_SetStatus(LED_SW1, net_led_status);
        
        return; // 处于配网状态时，霸占所有灯光，跳过后续业务渲染！
    }
		
    // ----------------------------------------------------
    // 1. 充放电指示 (优先级最高，关机状态下也需生效)
    // ----------------------------------------------------
    if (g_Device.power_mode == POWER_AC) {
        HAL_LED_SetStatus(LED_SW2, LED_OFF); 
				if (App_FSM_GetChargeState() == APP_CHARGE_DONE) {
            HAL_LED_SetStatus(LED_SW1, LED_ON);  
        } else {
            HAL_LED_SetStatus(LED_SW1, blink_slow ? LED_ON : LED_OFF); 
        }
    } else {
        HAL_LED_SetStatus(LED_SW1, LED_OFF); 
				/* 与APP上报共用滤波后的电量档位：0高、1中、2低。 */
			if ((g_Device.state != STATE_OFF) &&
					((HAL_ADC_Get_Battery_SOC() == 2U) ||
					 !App_FSM_BatteryPumpAllowed())) {
					/* 提醒档或停泵锁存时保持红灯，避免回弹后提示消失。 */
					HAL_LED_SetStatus(
							LED_SW2, blink_slow ? LED_ON : LED_OFF);
			} else {
					HAL_LED_SetStatus(LED_SW2, LED_OFF);
			}
    }
		
    // ----------------------------------------------------
    // 2. 关机清场检查
    // ----------------------------------------------------
    if (g_Device.state == STATE_OFF) {
        HAL_LED_SetStatus(LED_MODE_SENSE, LED_OFF);
        HAL_LED_SetStatus(LED_MODE_CONT,  LED_OFF);
        HAL_LED_SetStatus(LED_HEATING,    LED_OFF);
        HAL_LED_SetStatus(LED_WATER_Q,    LED_OFF);
        HAL_LED_SetStatus(LED_NO_WATER,   LED_OFF);
        
        /* 复用五灯刷新，关机清场仍在原来的位置执行。 */
        App_UI_SetSenseIndicators(LED_OFF);
        return;
    }

    // ----------------------------------------------------
    // 3. 普通业务渲染
    // ----------------------------------------------------
    ir_led_state = g_Device.uvc_armed ? LED_ON : LED_OFF;

    /* 复用同一刷新顺序，UVC指示仍由原来的许可标志决定。 */
    App_UI_SetSenseIndicators(ir_led_state);
    
    if (g_Device.display_timer_s > 0) {
        HAL_LED_SetStatus(LED_MODE_SENSE, (g_Device.target_work_mode == 0) ? LED_ON : LED_OFF);
        HAL_LED_SetStatus(LED_MODE_CONT,  (g_Device.target_work_mode == 1) ? LED_ON : LED_OFF);
    } else {
        HAL_LED_SetStatus(LED_MODE_SENSE, LED_OFF);
        HAL_LED_SetStatus(LED_MODE_CONT,  LED_OFF);
    }
            
    if (cached_temperature_c > 37) {
        HAL_LED_SetStatus(LED_HEATING, blink_slow ? LED_ON : LED_OFF); 
    } else {
        HAL_LED_SetStatus(LED_HEATING, g_Device.heating_enabled ? LED_ON : LED_OFF); 
    }

    HAL_LED_SetStatus(LED_WATER_Q, (g_Device.water_quality == WATER_BAD) ? LED_ON : LED_OFF);
     // 【修改】：缺水灯由极速响应标志驱动，不再等待状态机的2秒防误杀延迟
    HAL_LED_SetStatus(LED_NO_WATER, g_ui_water_empty ? LED_ON : LED_OFF);
}

/* 检查业务状态和显示需求；硬件、通信条件由main继续检查。 */
uint8_t App_FSM_StopStateReady(void)
{
    /* 软件关机沿用原有STOP规则。 */
    if (g_Device.state == STATE_OFF) {
        return 1;
    }

		/* 开机低电量时保持红灯闪烁，不能进入STOP关闭显示。 */
		if ((g_Device.power_mode == POWER_BATTERY) &&
				((HAL_ADC_Get_Battery_SOC() == 2U) ||
				 !App_FSM_BatteryPumpAllowed())) {
				return 0; /* 保持低电及停泵保护提示。 */
		}
		
    if (g_Device.state != STATE_IDLE ||
        g_Device.target_work_mode != 0) {
        return 0;
    }

    /* 活动倒计时、设置指示和配网显示期间保持正常调度。 */
    if (g_Device.timer_pump_s ||
        g_Device.timer_uvc_s ||
        g_Device.display_timer_s ||
        g_Device.wifi_pairing_timer_s ||
        g_Device.heating_enabled ||
        g_Device.uvc_armed) {
        return 0;
    }

    /* 同时覆盖缺水原始输入、业务状态和指示灯锁存。 */
    if (HAL_ADC_Get_IsEmpty() ||
        g_Device.is_empty ||
        g_ui_water_empty) {
        return 0;
    }

    /*
     * 检查业务值及最新采样缓存，避免ADC已更新、
     * FSM尚未更新的短暂窗口允许入睡。
     * Getter只读取缓存，不启动新采样。
     */
    if (g_Device.water_quality == WATER_BAD ||
        HAL_ADC_Get_WaterQuality() == WATER_BAD ||
        g_Device.sys_voltage_mv < 3500U ||
        HAL_ADC_Get_SystemVoltage_mV() < 3500U ||
        cached_temperature_c > 37) {
        return 0;
    }

    return 1;
}

/**
 * @brief 恢复出厂设置（清除本地用户数据并保存）
 */
void App_Restore_Factory_Settings(void) {
		g_Device.req_clean_alarm = 0;
		g_Device.req_filter_alarm = 0;
	
    // 1. 恢复各项耗材参数为默认值
    g_Device.target_clean_days = 7; 
    g_Device.target_filter_days = 14;
    g_Device.last_clean_timestamp = 0;
    g_Device.last_filter_timestamp = 0;
    
    // 3. 恢复默认声音状态（0:发声）
    g_Device.mute_enabled = 0;
    
    // 4. 清除云端同步标志，确保下次连云后重新触发全量上报
    g_cloud_data_synced = 0;

    // 5. 强制保存到 EEPROM，覆盖旧数据
    HAL_EEPROM_Save_AllData();

}