/*****************************************************************************************************
  * @copyright  (c)  Shenzhen Saiyuan Microelectronics Co., Ltd
  * @file       main.c
	
2026/6/26
TL2 T1	    功能变更		
1、调整TDS算法，使用差分融合法，做到TDS范围0-300间误差+-20
2、修改蜂鸣器双击蜂鸣间隔时间
3、修改充电中和低电量闪灯时间从500ms慢闪改为800ms
4、修改缺水逻辑解决缺水状态下按键以及灯光显示问题
5、修改感应灯亮灯逻辑，现在只在按键按下才会亮灯，不会因为感应到物体重新亮灯
6、修改缺水消抖逻辑，现在不进行缺水消抖"

2026/7/2	
TL2 T2	硬件变更软件适配		
1.调整TM1650延时开启
2、固定温度电压为3.3V
3.TDS加长延时，解决增加电容导致的充放电问题
4.增加TYPEC供电adc判断口，分离BAT_IO供电与充电逻辑"

2026/7/10	
TL2 T3	软件需求		
1.修改蜂鸣器声响间隔为100ms
2.增高缺水判断优先级保证灵敏度
3.增加38度过温报警功能"

2026/7/30
TL2_T4	软件需求		
1.增加WIFI功能	

2026/8/8
TL2_T4	软件需求		
1.饮水次数计入EEPROM
2.引入看门狗、通讯超时退出解决偶发性WIFI通讯卡死问题
3.红外增加2路，变为3路高低检测

2026/8/15
TL2_T10	软件需求		
1.感应出水时间修正（更换完一体板后时间锁死为3S，感应出参数改正为12
2.使用涂鸦module_reset_notify_handler函数，实现区分云端清除数据与恢复出厂设置功能
3.修改WIFI端报警逻辑，App_Cloud_Sync_Task修改实现目标要求						针对消息提醒和推送问题
																																	1、清洗提醒和推送---实时计算实时下发，推送时间为实时
																																	2、滤芯保养提醒和推送--实时计算实时下发，推送时间为实时
																																	3、水温异常提醒和推送--超38度时，固件实时上报，云端实时下发，推送实时下发
																																	4、缺水提醒和推送一临界点来回跳动，累计触发10次，或检测缺水稳定1分钟，固件上报一次TDS异
																																	上报一次TDS异常，开启30分钟固件上报冷却，云端开启3小时推送冷却
																																	5、TDS水质超标警告和推送-临界点来回跳动，累计触发10次，或检测到超标状态稳定1分钟，固件上报一次TDS异
																																	上报一次TDS异常，开启30分钟固件上报冷却，云端开启3小时推送冷却
																																	6、低电量提醒和推送--低电量触发后，固件上报冷却30分钟，30分钟内电压无回升，再次上报DP.推
																																	送下发冷却时间为3小时
4.增加了滤芯与清洁时间的时间跳变防呆if (days_passed > 0 && days_passed < 365)断电重启获取云端时间的瞬间，极易引发时间戳的断层跳变。清洗逻辑有 < 365 的防呆护城河，跳变的数据被抛弃了

2026/8/25
TL2_T11	软件需求		
1. // 重置WIFI后，强行留出至少3秒(180->177)的时间让模组离线刷新状态。
   // 严禁在此窗口期内采信模组旧的已连接状态，防止配网闪烁被瞬间清零！
   if (g_Device.wifi_pairing_timer_s < 177) {
       g_Device.wifi_pairing_timer_s = 0;  
       HAL_Buzzer_Beep(1); // 连云成功提示音
   }
2. 修改水温的上报逻辑，解决1s长时间频繁上报WIFI模组内存溢出卡死
3. 修复已配网设备重新配网时指示灯异常熄灭问题（重构配网状态机Task_Wifi_Pairing_Check，引入等待断开标志位替代硬延时，彻底解决涂鸦模组重启期间SDK旧状态缓存导致的“假连网”误判）
4. 修改UI渲染逻辑App_UI_Refresh_Task：屏蔽设备关机状态下的低电量(LED_SW2)闪烁报警，避免关机闪烁。
5. 修改按键反馈逻辑：设备在关机(STATE_OFF)状态下，丢弃所有的单击、双击意图并屏蔽触摸蜂鸣器声音。
6. 修改配网UI表现：当设备处于关机状态下被APP端移除并自动进入配网时，允许后台配网但禁止UI指示灯闪烁。	
7. 修复致命硬件保护缺陷（PWM高频斩波）：解决物理缺水保护与状态机2秒防误杀消抖产生的竞态冲突。在 App_FSM_Run 外设执行层引入 hardware_empty 标志位进行直接物理拦截，杜绝水面晃动时继电器/MOS管的高频开关。
8. 修复WiFi通讯卡死死锁隐患：删除了 protocol.c 中 get_wifi_status 里的同名局部静态变量 reset_notify_enabled，统一调用外部全局标志位 g_reset_notify_enabled，解决单片机因标志位不同步而每秒向模组狂发 0x04 0x00 指令造成的串口溢出死锁。
10. 修复模组重置通知(Reset Notify)协议错误：修正 module_reset_notify_handler 函数的 ACK 应答机制，将错误写死的 0x04 (启动请求) 修改为原样返回 cmd_bytes[0] (当前接收的子命令字)，彻底杜绝因“答非所问”引发的涂鸦模组超时重传风暴及 EEPROM 异常擦写。
11. 修复出厂EEPROM数据防呆硬编码错位：在 HAL_EEPROM_Load_AllData() 中，将出厂空片（0xFF）复位时的硬编码魔数 31 和 30，统一替换为顶层宏定义 DEFAULT_CLEAN_DAYS 与 DEFAULT_FILTER_DAYS，确保出厂默认配置与业务层设定完美对齐。
12. 解决了“水面一晃，加热就停”的 Bug，体验更连贯
    过去：机器过于敏感。宠物喝水或者机器轻微晃动，水面只要产生零点几秒的波纹，机器就会认为“缺水了”，瞬间停机，会把在 APP 上设置好的“加热”和“杀菌”功能关掉。
    现在：加入了“2秒智能防误杀”机制。水面轻微晃动时，机器依然会极速断电保护防止干烧，但不会再篡改客户的设定。水面一平静，机器自动恢复加热和杀菌。
13. 删除了饮水次数的统计
14. 增加 P02 供电迟滞防抖：系统电压<3350mV断电，>3500mV恢复，加入500ms防抖避免临界跳动。
15. 简化电量计算为三档：取消查表，改为>3.9V(100%)、3.5-3.9V(50%)、<3.5V(10%)三档。带负载补偿与3秒防抖，每次切档蜂鸣器长鸣3秒。
16. 同步修改WIFI电量上报：根据新的三档实时边沿上报，且仅在低电量档(10%)时保持30分钟循环推送。
17. 修正涂鸦协议电量DP配置：将 DPID_BAT 的协议类型由 VALUE(数值) 正式更正为 ENUM(枚举)。
18. 实现缺水指示灯的“极速响应”与“防抖锁存”机制，提升视觉反馈体验。
    过去：缺水灯受限于状态机的2秒防误杀逻辑，导致硬件已瞬间断电保护，但指示灯延迟1~2秒才亮，且水面波动时容易频繁闪烁。
    现在：将UI缺水灯控制与业务状态机解耦。只要物理层检测到缺水或水面晃动，缺水灯0延迟瞬间点亮并保持常亮（锁存状态）；仅当水面连续2秒真正稳定高水位时才熄灭。确保灯光反馈与硬件断电保护绝对同频，给用户明确的“正在防干烧保护”视觉提示。

2026/9/29  
TL2_T12　软件修改
1. 引入 Wi-Fi 低功耗管理，支持 APP 远程唤醒、命令处理及本地状态同步。
2. 引入 MCU STOP 低功耗，支持关机及电池供电下感应待机空闲休眠；开机时有报警显示或外设运行则保持正常工作。
3. 充电模块更换为 SC89601，适配 12V 输入，增加软件 IIC 通讯、充电状态检测、芯片看门狗及异常重试。
4. 调整引脚：水泵改为 P0.2，外设供电开关改为 P0.0，UV 改为 P5.1，电池 ADC 改为 P2.2，充电芯片 IIC 使用 P1.6/P1.7。
5. Type-C 检测改为 P5.0 数字输入，低电平表示插入；保留 P0.3 充电状态信号。
6. 红外检测合并为 P0.6 单路高电平有效，保留消抖及感应出水逻辑。
7. 完善清洗及滤芯耗材计时，支持休眠期间计时、到期唤醒处理、APP 上报及 EEPROM 保存恢复。
8. 清理临时测试代码、调试日志及未使用函数，降低 ROM 占用。

2026/10/27
TL2_T13    软件修改

1. 调整引脚：UV改为P1.3，Wi-Fi CEN改为P5.1。
2. 充电电流提高至900mA，调整频率配置，取消OTG控制。
3. 完善充电状态判断，本地指示区分充电、补电和充满。
4. 增加Wi-Fi通信异常后的CEN复位恢复，修复断网后无法再次进入STOP的问题。
5. 完善DP发送失败补报、影子同步及连续开关指令处理。
6. 电量分档调整为3.8V/3.6V，保留补偿、防抖及回差。
7. 电量DP增加三档充电枚举，充满显示High；追加Loww区分低电保护。
8. 低于3.5V确认后锁泵，300秒后关灯并关闭Wi-Fi；
   低于3.35V提前关机，插电解除保护并恢复通信。
9. 修正清洗、滤芯0天设置的保存恢复及周期起点处理。
10. RST重启时，在Wi-Fi CEN动作前增加3秒长鸣。
11. 增加日志总开关及分项开关，减少APROM占用。
12. 温度上报增加回差锁存，减少38/39℃重复触发。
13. 恢复普通关机时APP模式操作提示音，保护关机保持静默。

2026/10/27
TL2_T13    软件修改

1. 增加SC89601的降压频率配置功能
*****************************************************************************************************/ 
#include "tuya_type.h"
#include "SC_Init.h"
#include "SC_it.h"
#include "..\Drivers\SCDriver_list.h"
#include "HeadFiles\SysFunVarDefine.h"
#include "app_fsm.h"
#include "hal_pwm.h"
#include "hal_buzzer.h"
#include "hal_tm1650.h"
#include "hal_touch.h"
#include "hal_ir.h"
#include "hal_adc.h"
#include "hal_power.h"
#include "hal_uart.h"
#include "hal_lpd.h"
#include <stdio.h>
#include "wifi.h"
#include "protocol.h"
#include "app_wifi.h"
#include "sc89601.h"

/* 保留当前调试配置：1 跳过本文件的物理缺水保护；正式使用应设为 0。 */
#define DEBUG_IGNORE_WATER_EMPTY  0
/* 打印模式保持正常调度，避免电池供电时进入STOP而中断日志。 */
#define APP_MCU_STOP_ENABLE  1  /* 保留原STOP流程，与打印开关独立。 */

#define APP_MCU_STOP_IDLE_10MS   6000U  /* 名义空闲60秒后检查STOP资格。 */

/* Timer0 中断置位，主循环消费；秒任务使用 BTM，flag_1000ms 暂保留。 */
volatile uint8_t flag_10ms = 0;
volatile uint8_t flag_100ms = 0;
volatile uint8_t flag_500ms = 0;
volatile uint8_t flag_1000ms = 0;
extern bit TIM0Flag2;
extern uint8_t g_reset_notify_enabled;
uint8_t cached_is_empty = 0;

/* BOOT 阶段旧云同步的温度、水位、水质和电量记录。 */
static uint8_t last_reported_temp = 0xFF;
static uint8_t temp_sync_timer = 0;
static uint8_t tds_debounce_count = 0;
static uint8_t tds_stable_timer_s = 0;
static uint16_t tds_report_cooldown_s = 0;
static uint8_t last_water_quality = 0xFF;
static uint16_t bat_report_cooldown_s = 0;
static uint8_t last_bat_soc = 0xFF;

/* MCU 空闲计时、BTM 分频和睡眠巡检状态。 */
static uint16_t s_power_idle_10ms = 0;
static uint8_t s_power_sleeping = 0;
static uint8_t s_power_second_div = 0;
static uint8_t s_power_adc_div = 0;
static uint8_t s_power_adc_due = 0;
static uint8_t s_power_input_seen = 0;

/* 诊断记录只驻留RAM；未连接调试器时仍累计，复位后清零。 */
#if APP_LOG_POWER_ON
static uint16_t s_power_dbg_stop = 0U;
static uint16_t s_power_dbg_exit = 0U;
static uint16_t s_power_dbg_clear = 0U;
static uint16_t s_power_dbg_prev = 0U;
static uint8_t s_power_dbg_block = 0U;
static uint8_t s_power_dbg_wake = 0U;
static uint8_t s_power_dbg_io = 0U;
static uint8_t s_power_dbg_clear_reason = 0U;
/* 当前10ms周期收集的输入，以及最近一次清零对应的输入。 */
static uint8_t s_power_dbg_input_seen = 0U;
static uint8_t s_power_dbg_clear_input = 0U;
/* 记录阻塞分组，同时保持原来的返回0语义。 */
#define APP_POWER_DBG_BLOCK(n) (s_power_dbg_block = (n), 0U)

/* 计数达到65535后保持，避免长时间运行后回绕。 */
#define APP_POWER_DBG_COUNT(v) do { \
    if ((v) < 0xFFFFU) { ++(v); } \
} while (0)

/* 只统计确实丢弃了非零空闲进度的清零。 */
#define APP_POWER_DBG_CLEAR(n) do { \
    if (s_power_idle_10ms != 0U) { \
        APP_POWER_DBG_COUNT(s_power_dbg_clear); \
        s_power_dbg_prev = s_power_idle_10ms; \
        s_power_dbg_clear_reason = (n); \
        /* 仅将输入快照关联到cr=6，避免其他清零原因沿用旧值。 */ \
        s_power_dbg_clear_input = ((n) == 6U) ? \
            s_power_dbg_input_seen : 0U; \
    } \
} while (0)
#else
#define APP_POWER_DBG_BLOCK(n) 0U
#define APP_POWER_DBG_COUNT(v) do { } while (0)
#define APP_POWER_DBG_CLEAR(n) do { } while (0)
#endif

/* 入睡前保存引脚、电池电压和 ADC 开启状态，供巡检及唤醒恢复使用。 */
static uint8_t s_power_water_before = 0;
static uint8_t s_power_bat_io_before = 0;
static uint16_t s_power_battery_before = 0;
static uint8_t s_power_adc_enabled = 0;

#define POWER_BAT_LOW_MV       3350U
#define POWER_BAT_CHANGE_MV    100U

/* 废弃旧同步记录；稳定计时预置 60 秒，使启动时已有的异常可立即进入上报判断。 */
void App_Cloud_Sync_Reset(void)
{
    g_Device_Shadow.state = 0xFF;
    g_Device_Shadow.target_work_mode = 0xFF;
    g_Device_Shadow.is_empty = 0xFF;
	App_Wifi_WaterSyncSubmit(); /* 重新同步已确认水位，保留原确认进度。 */
    g_Device_Shadow.water_quality = 0xFF;
    g_Device_Shadow.uvc_armed = 0xFF;
    g_Device_Shadow.heating_enabled = 0xFF;
    g_Device_Shadow.mute_enabled = 0xFF;

    tds_debounce_count = 0;
    tds_report_cooldown_s = 0;
    tds_stable_timer_s = 60;
    last_water_quality = 0xFF;

    last_reported_temp = 0xFF;
    temp_sync_timer = 0;
    last_bat_soc = 0xFF;
    bat_report_cooldown_s = 0;
    g_cloud_data_synced = 0;
}

/* BOOT 阶段每秒调用；沿用 SDK 返回值更新同步记录，不代表云端确认。 */
void App_Cloud_Sync_Task(void)
{
    static uint8_t sync_wait_s = 0;
    uint8_t current_temp;

    if (mcu_get_wifi_work_state() != WIFI_CONN_CLOUD) {
        return;
    }

    /* 30 秒后置同步标志；当前代码不会用该标志阻止下面的 DP 上报。 */
    if (!g_cloud_data_synced) {
        sync_wait_s++;
        if (sync_wait_s >= 30) {
            g_cloud_data_synced = 1;
        }
    } else {
        sync_wait_s = 0;
    }

    /* 总开关上报：OFF 为 0，其余设备状态均为 1。 */
    if (g_Device.state != g_Device_Shadow.state) {
        if (SUCCESS == App_Wifi_ReportBool(DPID_SWITCH,
                                         (g_Device.state != STATE_OFF))) {
            g_Device_Shadow.state = g_Device.state;
        }
    }

    /* BOOT阶段每秒调用；本地发送成功后更新同步记录，不代表云端确认。 */
		if (g_Device.target_work_mode != g_Device_Shadow.target_work_mode) {
				/* 修复：三项模式DP共同确认，失败保留待发。 */
				App_Wifi_ReportMode();
		}

    /* TDS 报警沿用相同的次数、稳定时间和冷却规则；恢复正常时解除冷却并同步。 */
    if (last_water_quality == 0xFF) {
        last_water_quality = g_Device.water_quality;
    } else if (g_Device.water_quality != last_water_quality) {
        tds_debounce_count++;
        last_water_quality = g_Device.water_quality;
    }

    if (g_Device.water_quality == WATER_BAD) {
        if (tds_stable_timer_s < 0xFFU) {
            tds_stable_timer_s++;
        }
    } else {
        tds_stable_timer_s = 0;
        tds_report_cooldown_s = 0;
        if (g_Device_Shadow.water_quality != g_Device.water_quality) {
            if (SUCCESS == App_Wifi_ReportEnum(DPID_TDS_RO,
                                             g_Device.water_quality)) {
                g_Device_Shadow.water_quality = g_Device.water_quality;
                tds_debounce_count = 0;
            }
        }
    }

    if ((tds_debounce_count >= 10) || (tds_stable_timer_s >= 60)) {
        if ((g_Device.water_quality == WATER_BAD) &&
            (tds_report_cooldown_s == 0)) {
            if (SUCCESS == App_Wifi_ReportEnum(DPID_TDS_RO,
                                             g_Device.water_quality)) {
                g_Device_Shadow.water_quality = g_Device.water_quality;
                tds_report_cooldown_s = 1800;
            }
        }
        tds_debounce_count = 0;
        tds_stable_timer_s = 0;
    }
    if (tds_report_cooldown_s > 0) {
        tds_report_cooldown_s--;
    }

    /* 同步 UVC 武装设置和加热设置。 */
    if (g_Device.uvc_armed != g_Device_Shadow.uvc_armed) {
        if (SUCCESS == App_Wifi_ReportBool(DPID_UV, g_Device.uvc_armed)) {
            g_Device_Shadow.uvc_armed = g_Device.uvc_armed;
        }
    }
    if (g_Device.heating_enabled != g_Device_Shadow.heating_enabled) {
        if (SUCCESS == App_Wifi_ReportBool(DPID_HEAT, g_Device.heating_enabled)) {
            g_Device_Shadow.heating_enabled = g_Device.heating_enabled;
        }
    }

			/*
			 * 跨阈值上报统一由App_Wifi_TempObserve确认。
			 * 此处只保留BOOT阶段原有的普通温度同步。
			 */
    /* BOOT只保留一份60秒同步，避免重复累加缩短检查间隔。 */
    current_temp = HAL_ADC_Get_Temperature_C();

    if (++temp_sync_timer >= 60U) {
        temp_sync_timer = 0U;

        /* 首次正常同步，其余过滤相对上次上报值的1℃变化。 */
        if (last_reported_temp == 0xFFU ||
            ((current_temp >= last_reported_temp) ?
             (current_temp - last_reported_temp) :
             (last_reported_temp - current_temp)) >= 2U) {
            if (SUCCESS == App_Wifi_ReportValue(
                    DPID_TEMP_CURRENT, (u32)current_temp)) {
                last_reported_temp = current_temp;
            }
        }
    }

    /* BOOT同步使用同一套六档显示值。 */
    {
        uint8_t current_soc;
        current_soc = App_Wifi_GetBatDpValue();

        if (current_soc != WIFI_BAT_INVALID) {
            if (current_soc != WIFI_BAT_LOW) {
                bat_report_cooldown_s = 0;
            }

            if (current_soc != last_bat_soc ||
                (current_soc == WIFI_BAT_LOW &&
                 bat_report_cooldown_s == 0)) {
                if (SUCCESS == App_Wifi_ReportEnum(
                        DPID_BAT, current_soc)) {
                    last_bat_soc = current_soc;
                    bat_report_cooldown_s =
                        (current_soc == WIFI_BAT_LOW) ? 1800U : 0U;
                }
            }

            if (bat_report_cooldown_s > 0) {
                bat_report_cooldown_s--;
            }
        }
    }

    if (g_Device.mute_enabled != g_Device_Shadow.mute_enabled) {
        if (SUCCESS == App_Wifi_ReportBool(DPID_BEEP, g_Device.mute_enabled)) {
            g_Device_Shadow.mute_enabled = g_Device.mute_enabled;
        }
    }
}

extern volatile uint8_t g_uvc_exti_flag;
static uint8_t s_power_display_restore = 0;

/* 同时检查实时输入和中断锁存，避免未处理的输入事件被睡眠延迟。 */
static uint8_t App_Power_InputActive(void)
{
    if (HAL_IR_IsActive()) {
        return 1;
    }
    if (P00 && ((P07 == 0) || (P11 == 0))) {
        return 1;
    }
    if (g_uvc_exti_flag || HAL_Power_InputPending()) {
        return 1;
    }
		/* CEN关闭期间，模组唤醒线不再作为有效通信事件。 */
		if (P20 && !WiFi_CEN_IsHeld()) {
				return 1;
		}
				return 0;
}

/* 业务允许休眠且没有输入、负载活动时，累计空闲时间。 */
/* 业务允许休眠且没有输入、负载活动时，累计空闲时间。 */
static void App_Power_IdleTick10ms(void)
{
    uint8_t reason;

    reason = 0U;

    /* 保留原判断顺序，记录本次清零的第一个条件。 */
    if (!App_FSM_StopStateReady()) reason = 1U;
    else if (g_Device.power_mode != POWER_BATTERY) reason = 2U;
    else if (HAL_PWM_Pump_IsRunning()) reason = 3U;
    else if (P02) reason = 4U;
    else if (HAL_IR_IsActive()) reason = 5U;
    else if (s_power_input_seen) reason = 6U;

    if (reason != 0U) {
        APP_POWER_DBG_CLEAR(reason);
        s_power_idle_10ms = 0U;
    } else if (s_power_idle_10ms < APP_MCU_STOP_IDLE_10MS) {
        s_power_idle_10ms++;
    }

    s_power_input_seen = 0U;
#if APP_LOG_POWER_ON
    /* 本轮已完成判断，下一轮重新收集输入快照。 */
    s_power_dbg_input_seen = 0U;
#endif
}

/* 入睡前检查；诊断记录实际遇到的第一个阻塞分组。 */
static uint8_t App_Power_BaseReady(void)
{
    if (!sc89601_can_stop()) {
        return APP_POWER_DBG_BLOCK(1U);
    }

    /* 拆开原有短路判断，仅增加阻塞原因记录。 */
    if (!App_FSM_StopStateReady()) {
        return APP_POWER_DBG_BLOCK(2U);
    }
    if (!App_Wifi_McuStopReady()) {
        return APP_POWER_DBG_BLOCK(3U);
    }

    if (s_power_idle_10ms < APP_MCU_STOP_IDLE_10MS) {
        return APP_POWER_DBG_BLOCK(4U);
    }

    if ((g_Device.power_mode != POWER_BATTERY) ||
        HAL_PWM_Pump_IsRunning() || P02 || HAL_IR_IsActive()) {
        return APP_POWER_DBG_BLOCK(5U);
    }

    if (HAL_Touch_IsBusy() || HAL_Buzzer_IsBusy() ||
        App_Power_InputActive()) {
        return APP_POWER_DBG_BLOCK(6U);
    }

    if (P10 ||
#if !APP_CHARGE_DEBUG
        P13 ||
#endif
        g_Device.timer_pump_s || g_Device.timer_uvc_s ||
        g_Device.display_timer_s || g_Device.wifi_pairing_timer_s) {
        return APP_POWER_DBG_BLOCK(7U);
    }

    if (g_Device.req_power_toggle || g_Device.req_mode_toggle ||
        g_Device.req_heating || g_Device.req_uvc ||
        g_Device.req_wifi_reset || g_Device.req_cancel_pairing) {
        return APP_POWER_DBG_BLOCK(8U);
    }

    (void)APP_POWER_DBG_BLOCK(0U);
    return 1U;
}

/* 正常主循环秒任务调用；STOP巡检和ISR中均不打印。 */
static void App_Power_Debug1s(void)
{
#if APP_LOG_POWER_ON
    /* 与WLP错开打印，减少集中占用主循环。 */
    static uint8_t divider = 2U;

    if (++divider < 5U) {
        return;
    }
    divider = 0U;

    printf("\r\n[MLP] en=%u idle=%u b=%u st=%u pm=%u mv=%u soc=%u\r\n",
        (unsigned int)APP_MCU_STOP_ENABLE,
        (unsigned int)s_power_idle_10ms,
        (unsigned int)s_power_dbg_block,
        (unsigned int)g_Device.state,
        (unsigned int)g_Device.power_mode,
        (unsigned int)HAL_ADC_Get_SystemVoltage_mV(),
        (unsigned int)HAL_ADC_Get_Battery_SOC());

		printf("[MPE] stop=%u out=%u wr=%u io=%02X clr=%u cr=%u prev=%u ei=%02X\r\n",
				(unsigned int)s_power_dbg_stop,
				(unsigned int)s_power_dbg_exit,
				(unsigned int)s_power_dbg_wake,
				(unsigned int)s_power_dbg_io,
				(unsigned int)s_power_dbg_clear,
				(unsigned int)s_power_dbg_clear_reason,
				(unsigned int)s_power_dbg_prev,
				(unsigned int)s_power_dbg_clear_input);
#endif
}

/* BTM每8个Tick推进一秒，包含STOP巡检期间。 */
static void App_Power_ClockTask(void)
{
    uint16_t ticks;

    ticks = HAL_Power_TakeTicks();

    while (ticks != 0) {
        ticks--;

        if (s_power_sleeping) {
            if (++s_power_adc_div >= 4) {
                s_power_adc_div = 0;
                s_power_adc_due = 1;
            }
        }

        if (++s_power_second_div >= 8) {
            s_power_second_div = 0;

            /* 当前在主循环中，可执行充电芯片I2C维护。 */
            sc89601_tick_1s();
						App_FSM_BatteryTick_1s(); /* 放在睡眠分支之前，保证休眠期间也计时。 */
				   /* 修复：水位确认和重试按秒推进，STOP巡检也执行。 */
            App_Wifi_WaterTick1s();
						App_Wifi_TimeSyncTick1s(); /* 修复：STOP巡检期间也推进校时重试。 */
            if (s_power_sleeping) {
                if (g_Device.current_unix_time != 0) {
                    g_Device.current_unix_time++;
                }
            } else {
                App_FSM_Tick_1s();

                if (App_WifiTest_IsBoot()) {
                    App_Cloud_Sync_Task();
                }

                App_WifiTest_Tick1s();
								App_Power_Debug1s(); /* 输出MCU诊断，不放进公共STOP巡检路径。 */
            }
        }
    }
}

/* 先结算睡眠 Tick，再恢复 ADC/Timer0；强制采样和 FSM/UI 更新后恢复显示。 */
/* 保存退出原因，再恢复正常调度；不在这里打印。 */
static void App_Power_Exit(uint8_t reason)
{
#if APP_LOG_POWER_ON
    APP_POWER_DBG_COUNT(s_power_dbg_exit);
    s_power_dbg_wake = reason;

    /* 退出当时的引脚快照，不等同于中断来源。 */
    s_power_dbg_io =
        (P20 ? 0x01U : 0U) | (P06 ? 0x02U : 0U) |
        (P07 ? 0x04U : 0U) | (P11 ? 0x08U : 0U) |
        (P01 ? 0x10U : 0U) | (P03 ? 0x20U : 0U) |
        (P50 ? 0x40U : 0U) | (P02 ? 0x80U : 0U);
#else
    (void)reason;
#endif

    App_Power_ClockTask();
    s_power_sleeping = 0;

    APP_POWER_DBG_CLEAR(7U); /* 睡眠流程退出时也记录空闲清零。 */
    s_power_idle_10ms = 0;

    s_power_adc_div = 0;
    s_power_adc_due = 0;
    s_power_input_seen = 0;

    HAL_Power_TakeInputEvent();
    flag_10ms = 0;
    flag_100ms = 0;
    flag_500ms = 0;
    flag_1000ms = 0;

    if (s_power_adc_enabled) {
        ADCCON |= 0x80;
    }

    HAL_Power_Resume();
    flag_500ms = 1;
    flag_100ms = 1;
    s_power_display_restore = 1;
}

/* 入睡流程沿用原检查，增加首次STOP调用记录。 */
static void App_Power_TryEnter(void)
{
    if (!App_Power_BaseReady()) {
        return;
    }

    if (flag_10ms || flag_100ms || flag_500ms) {
        (void)APP_POWER_DBG_BLOCK(9U);
        return;
    }

    HAL_Power_Prepare();

    /* 拆开原有短路判断，分别记录准入失败和时间片阻塞。 */
    if (!App_Power_BaseReady()) {
        HAL_Power_Resume();
        return;
    }
    if (flag_10ms || flag_100ms || flag_500ms) {
        (void)APP_POWER_DBG_BLOCK(9U);
        HAL_Power_Resume();
        return;
    }

    s_power_water_before = P01;
    s_power_bat_io_before = P03;
    s_power_battery_before = HAL_ADC_Get_SystemVoltage_mV();
    s_power_adc_enabled = ADCCON & 0x80;
    s_power_adc_div = 0;
    s_power_adc_due = 0;

    P24 = 0;
    P25 = 0;
    HAL_TM1650_PowerOff();
    s_power_display_restore = 1;
    ADCCON &= 0x7F;
    s_power_sleeping = 1;

    if (!App_Wifi_McuStopReady() || App_Power_InputActive() ||
        (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        App_Power_Exit(1U); /* 最终复查中止，还没有调用STOP。 */
        return;
    }

    /* 每轮睡眠只统计首次调用，不累计BTM巡检后的再次STOP。 */
    APP_POWER_DBG_COUNT(s_power_dbg_stop);
    HAL_Power_StopOnce();
}

/*
 * 每次硬件唤醒后巡检。
 * 命中退出条件时记录原因，否则喂狗后继续STOP。
 */
static void App_Power_SleepTask(void)
{
    uint8_t event;
    uint16_t battery_mv;
    uint16_t change_mv;

    App_Power_ClockTask();

		if (App_FSM_BatteryShutdownActive() &&
				(P00 || g_Device.state != STATE_OFF ||
				 !WiFi_CEN_IsHeld())) {
				/* 到期后退出STOP一次，确保执行关闭Wi-Fi的动作。 */
				App_Power_Exit(2U);
				return;
		}

    /* 耗材到期时恢复正常调度，交给原秒任务结算。 */
    if (App_FSM_ConsumableUpdateDue()) {
        App_Power_Exit(3U); /* 记录：耗材需要结算。 */
        return;
    }

    event = HAL_Power_TakeInputEvent();

    if (event ||
        !App_Wifi_McuStopReady() ||
        App_Power_InputActive() ||
        (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        /* 4：输入事件锁存；5：通信、实时输入或引脚变化。 */
        App_Power_Exit(event ? 4U : 5U);
        return;
    }

    /*
     * 每500ms巡检电池和Type-C。
     * 采样失败、低压或电压变化达到阈值时退出睡眠。
     */
    if (s_power_adc_due) {
        s_power_adc_due = 0;
        WDT_SetReload();

        if (sc89601_sleep_wake_required()) {
            App_Power_Exit(6U); /* 记录：充电驱动要求恢复正常调度。 */
            return;
        }

        if (!HAL_ADC_PowerPoll(&battery_mv)) {
            App_Power_Exit(7U); /* 记录：睡眠电压采样失败。 */
            return;
        }

        if (battery_mv >= s_power_battery_before) {
            change_mv = battery_mv - s_power_battery_before;
        } else {
            change_mv = s_power_battery_before - battery_mv;
        }

        if (HAL_Power_TypeCIsPresent() ||
            (!App_FSM_BatteryShutdownActive() &&
             ((battery_mv < POWER_BAT_LOW_MV) ||
              ((g_Device.state != STATE_OFF) &&
               (battery_mv < 3600U)) ||
              (change_mv >= POWER_BAT_CHANGE_MV)))) {
            /* 保留原保护判断，仅增加电源/电压退出原因记录。 */
            App_Power_Exit(8U);
            return;
        }

        /*
         * 保护关机后，低压及电压回弹不再反复唤醒；
         * 插电检测仍有效。
         */
    }

    if (!App_Wifi_McuStopReady() ||
        App_Power_InputActive() ||
        (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        App_Power_Exit(9U); /* 记录：巡检后复查发现通信、输入或引脚变化。 */
        return;
    }

    WDT_SetReload();
    HAL_Power_StopOnce();
}

extern volatile sc_u8 g_sc89601_debug_ready;

/*
 * 每100ms调用，每2秒启动一帧。
 * 先保存完整快照，再分批打印，避免一次长时间占用主循环。
 */
static void App_Charge_Debug100ms(void)
{
#if APP_LOG_CHARGE_ON
    static sc_u8 divider = 0u;
    static sc_u8 phase = 0u;
    static sc_u8 reg_index = 0u;
    static sc89601_snapshot_t frame;
    sc_u8 count;
		static sc_u8 frame_init_result = SC89601_PENDING;
		static sc_u8 frame_poll_result = SC89601_PENDING;
    if (++divider >= 20u) { /* 100ms × 20：每2秒启动一组日志。 */
        divider = 0u;
    }

    if (phase == 0u) {
        if (divider != 0u) {
            return;
        }

        printf("\r\n[CHGR] init=%u poll=%u new=%u\r\n",
            (unsigned int)g_sc89601_init_result,
            (unsigned int)g_sc89601_poll_result,
            (unsigned int)g_sc89601_debug_ready);

        if (!g_sc89601_debug_ready) {
            return; /* 无完整新快照时，只输出驱动结果。 */
        }

        /* 固定本帧数据，避免分批打印时混入下一轮快照。 */
        frame = g_sc89601_snapshot;
				/* 驱动结果与寄存器快照一起保存，保证本帧状态解释一致。 */
				frame_init_result = g_sc89601_init_result;
				frame_poll_result = g_sc89601_poll_result;
				
        g_sc89601_debug_ready = 0u;

        reg_index = 0u;
        phase = 1u;
        return;
    }

    if (phase == 1u) {
        printf("[CREG]");
        count = 0u;

        /* 每次最多打印4个寄存器，给其他主循环任务留出机会。 */
        while ((count < 4u) &&
               (reg_index < SC89601_REG_COUNT)) {

            printf(" R%02X=%02X",
                (unsigned int)reg_index,
                (unsigned int)frame.reg[reg_index]);

            reg_index++;
            count++;
        }

        printf("\r\n");

        if (reg_index >= SC89601_REG_COUNT) {
            phase = 2u;
        }
        return;
    }

		if (phase == 2u) {
				printf("[CSTA] vbus=%u pg=%u gd=%u CHG=",
						(unsigned int)frame.vbus_status,
						(unsigned int)frame.power_good,
						(unsigned int)frame.vbus_good);

				if (!frame.vbus_good) {
						printf("NO_INPUT");
				} else if ((frame.fault_current & 0xFCu) != 0u) {
						/* 与业务层使用相同的主要故障掩码。 */
						printf("FAULT");
				} else if (!frame.power_good ||
									 frame_init_result != SC89601_OK ||
									 frame_poll_result != SC89601_OK) {
						printf("WAIT_READY");
				} else if (frame.charge_status != 0u &&
									 (frame.reg[SC89601_REG_STAT2] & 0x08u) != 0u) {
						/* 补充计时优先显示，避免阶段码3被提前解释为完成。 */
						printf("TOP_OFF");
				} else {
						switch (frame.charge_status) {
								case 0u:
										printf("NOT_CHARGING");
										break;

								case 1u:
										printf("PRECHARGE");
										break;

								case 2u:
										printf("FAST_CHARGE");
										break;

								case 3u:
										/* 前面已排除故障、未就绪和Top-off仍计时的情况。 */
										printf("CHARGE_DONE");
										break;

								default:
										printf("UNKNOWN");
										break;
						}
				}

				/* 保留原始阶段码及Top-off位，方便核对状态解释。 */
				printf(" raw=%u top=%u\r\n",
						(unsigned int)frame.charge_status,
						(unsigned int)(
								(frame.reg[SC89601_REG_STAT2] & 0x08u) != 0u));

				phase = 3u;
				return;
		}

    printf("[CFLT] lat=%02X now=%02X\r\n",
        (unsigned int)frame.fault_latched,
        (unsigned int)frame.fault_current);

    phase = 0u;
#endif
}


/* 主循环执行：先复位Wi-Fi，再复位MCU，保留EEPROM及配网信息。 */
static void App_ManualRestart(void)
{
    uint16_t ticks;
    uint8_t wait_ticks;

    HAL_PWM_Pump_Stop();
    P02 = 0; /* 直接关闭泵输出，覆盖强制开泵调试宏。 */
    HAL_PWM_Heating_Stop();
    HAL_UVCING_Stop();
    P00 = 0; /* 重启等待期间关闭外设供电，阻止按键继续产生业务请求。 */

				/* 先形成与重新启动一致的关机状态。 */
		App_FSM_Init();
		g_Device.target_work_mode = 0U; /* 重启后默认感应模式。 */
		g_Device.mute_enabled = 0U; /* 提前恢复重启后的有声状态。 */
		g_wifi_allow_sleep = 0U;
		WiFi_Module_KeepAwake();

		/* 在线且未处于休眠握手时，提前上报本次重启的最终状态。 */
		if (mcu_get_wifi_work_state() == WIFI_CONN_CLOUD &&
				g_wifi_lp_state != WIFI_LP_SLEEP_PENDING &&
				WiFi_UART_IsTxReady() &&
				!WiFi_UART_HasTxFault()) {

				WDT_SetReload();

				(void)App_Wifi_ReportBool(
						DPID_SWITCH, g_Device.state != STATE_OFF);

				App_Wifi_ReportMode(); /* 同步工作模式及两个关联开关。 */

				(void)App_Wifi_ReportBool(
						DPID_HEAT, g_Device.heating_enabled);

				(void)App_Wifi_ReportBool(
						DPID_UV, g_Device.uvc_armed);

				(void)App_Wifi_ReportBool(
						DPID_BEEP, g_Device.mute_enabled);
		}
		
    (void)HAL_Power_TakeTicks(); /* 丢弃旧节拍，从本次提示音重新计时。 */
    wait_ticks = 32U; /* 约4秒的等待上限，避免蜂鸣器计时停滞后无法重启。 */
    WDT_SetReload();
		
    HAL_Buzzer_BeepMs(3000U);

    /* 保持Timer0中断运行，等待原有1ms任务完成3秒长鸣。 */
    while (HAL_Buzzer_IsBusy() && (wait_ticks != 0U)) {
        ticks = HAL_Power_TakeTicks();

        if (ticks != 0U) {
            WDT_SetReload(); /* 时基推进时喂狗，允许正常完成重启等待。 */

            if (ticks >= wait_ticks) {
                wait_ticks = 0U;
            } else {
                wait_ticks -= (uint8_t)ticks;
            }
        }
    }

    HAL_Buzzer_BeepMs(0U); /* 拉低CEN前结束鸣叫，超时情况也强制关闭。 */

    WiFi_Module_KeepAwake();
    WiFi_CEN_Assert();

#if APP_LOG_POWER_ON
    printf("\r\n[RST] manual restart, WiFi CEN LOW\r\n");
#endif

    (void)HAL_Power_TakeTicks(); /* 丢弃旧节拍，从本次CEN拉低重新计时。 */
    wait_ticks = 16U;          /* 16个125ms节拍，约2秒。 */
    WDT_SetReload();

    while (wait_ticks != 0U) {
        ticks = HAL_Power_TakeTicks();

        if (ticks != 0U) {
            WDT_SetReload(); /* 时基有推进才喂狗，避免时基失效后永久等待。 */

            if (ticks >= wait_ticks) {
                wait_ticks = 0U;
            } else {
                wait_ticks -= (uint8_t)ticks;
            }
        }
    }

    EA = 0;
    IAPKEY = 0U;
    IAPADE = 0U;
    IAPCTL = 0U;  /* BTLD=0，软件复位后从APROM主程序启动。 */
    PCON = 0x08U; /* PCON只写，直接置RST位，不能用读改写。 */

    while (1) {
        /* 正常不会执行到这里；不再喂狗，保留WDT兜底。 */
    }
}


/* 初始化硬件、协议和时基，按时间片调度业务。 */
void main(void)
{
    volatile uint32_t boot_delay_i = 300000;
    volatile uint32_t boot_delay_j = 300000;

    while (boot_delay_i--);

    IcResourceInit();
    WiFi_WakeupPin_Init();
		WiFi_CEN_Release(); /* 启动时释放P5.1，并清除软件CEN保持标志。 */
#if defined(HIBERNATE_ENABLE)
    g_wifi_allow_sleep = 0;
    g_wifi_lp_state = WIFI_LP_UNKNOWN;
    g_wifi_sleep_reason = 0xFF;
#endif

    P00 = 0;
    HAL_UART_Init();
#if APP_LOG_POWER_ON
printf("\r\n[BOOT] MCU start\r\n");
#endif
    HAL_Buzzer_Init();

    while (boot_delay_j--);

    HAL_TM1650_Init();
    HAL_Touch_Init();
    HAL_IR_Init();
    App_FSM_Init();

    SC_USCI2_Init();
    wifi_protocol_init();
    App_WifiTest_Init();
    sc89601_service_init();

    WDT_Init(WDT_OverflowTime_500MS);
    WDT_Cmd(ENABLE);
    HAL_Power_Init();

    while (1) {
        if (s_power_sleeping) {
            App_Power_SleepTask();
            continue; /* 跳过下面所有正常任务，开始下一轮while */
        }

				if (HAL_Power_TakeInputEvent()) {
						s_power_input_seen = 1;

				#if APP_LOG_POWER_ON
						/* 10ms任务执行前可能经过多轮主循环，合并保留各轮事件。 */
						s_power_dbg_input_seen |= HAL_Power_GetInputDebug();
				#endif
				}

		/* 保护关闭模组后，不再解析旧会话或噪声数据。 */
		if (!App_FSM_BatteryShutdownActive()) {
				wifi_uart_service();
		}

        App_Power_ClockTask();
        if (flag_10ms) {
            flag_10ms = 0;
            HAL_IR_Scan();
            App_Power_IdleTick10ms();
        }

        /* ADC先于FSM执行，供同轮业务读取。 */
        if (flag_500ms) {
            flag_500ms = 0;
            HAL_ADC_Sample_All();
        }

				if (flag_100ms) {
						flag_100ms = 0;

						sc89601_service_100ms(); /* 更新充电及升压确认状态。 */

				#if APP_CHARGE_DEBUG
						App_Charge_Debug100ms();
				#endif

						App_FSM_Run(); /* 先处理开关机、感应和出水请求。 */

						App_UI_Refresh_Task();
						HAL_PWM_Pump_Constant_Voltage(
								g_Device.sys_voltage_mv);

						if (s_power_display_restore) {
								if (P00 && !App_FSM_BatteryShutdownActive()) {
										HAL_TM1650_PowerOn(); /* 供电有效且未保护关机时才恢复显示。 */
								}
								s_power_display_restore = 0;
						}
				}

#if !DEBUG_IGNORE_WATER_EMPTY
        if (P01 == 1) {
            HAL_PWM_Pump_Stop();
            HAL_PWM_Heating_Stop();
            HAL_UVCING_Stop();
        }
#endif
				if (HAL_Touch_RestartRequested()) {
						if (App_FSM_BatteryShutdownSyncActive() ||
								App_FSM_BatteryShutdownActive()) {
								HAL_Touch_Init();  /* 清除本次长按重启请求。 */
						} else if (stop_update_flag != ENABLE) {
								App_ManualRestart();
						}
				}
        App_WifiTest_Task();
        WDT_SetReload();

#if APP_MCU_STOP_ENABLE
        App_Power_TryEnter();
#endif
    }
}