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
  ***************************************************************************************************/ 
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
#define DEBUG_IGNORE_WATER_EMPTY  1
#define APP_MCU_STOP_ENABLE      0  /* 0 禁止主动进入 MCU STOP。 */
#define APP_MCU_STOP_IDLE_10MS     6000U   /* 测试10秒，验收后恢复6000U。 */

/* Timer0 中断置位，主循环消费；秒任务使用 BTM，flag_1000ms 暂保留。 */
volatile uint8_t flag_10ms = 0;
volatile uint8_t flag_100ms = 0;
volatile uint8_t flag_500ms = 0;
volatile uint8_t flag_1000ms = 0;
extern bit TIM0Flag2;
extern uint8_t g_reset_notify_enabled;
uint8_t cached_is_empty = 0;

extern volatile unsigned char g_wifi_reset_dbg_pending;

/* BOOT 阶段旧云同步的温度、水位、水质和电量记录。 */
static uint8_t last_temp_state = 0xFF;
static uint8_t last_reported_temp = 0xFF;
static uint8_t temp_sync_timer = 0;
static uint8_t empty_debounce_count = 0;
static uint16_t empty_stable_timer_s = 0;
static uint16_t empty_report_cooldown_s = 0;
static uint8_t last_is_empty = 0xFF;
static uint8_t tds_debounce_count = 0;
static uint16_t tds_stable_timer_s = 0;
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

/* 入睡前保存引脚、电池电压和 ADC 开启状态，供巡检及唤醒恢复使用。 */
static uint8_t s_power_water_before = 0;
static uint8_t s_power_bat_io_before = 0;
static uint16_t s_power_battery_before = 0;
static uint8_t s_power_adc_enabled = 0;

#define POWER_BAT_LOW_MV       3350U
#define POWER_BAT_CHANGE_MV    100U

/* OTG验证完成后改为0，关闭调试打印。 */
#define APP_CHARGE_DEBUG  1

/* 废弃旧同步记录；稳定计时预置 60 秒，使启动时已有的异常可立即进入上报判断。 */
void App_Cloud_Sync_Reset(void)
{
    g_Device_Shadow.state = 0xFF;
    g_Device_Shadow.target_work_mode = 0xFF;
    g_Device_Shadow.is_empty = 0xFF;
    g_Device_Shadow.water_quality = 0xFF;
    g_Device_Shadow.uvc_armed = 0xFF;
    g_Device_Shadow.heating_enabled = 0xFF;
    g_Device_Shadow.mute_enabled = 0xFF;

    empty_debounce_count = 0;
    empty_report_cooldown_s = 0;
    empty_stable_timer_s = 60;
    last_is_empty = 0xFF;

    tds_debounce_count = 0;
    tds_report_cooldown_s = 0;
    tds_stable_timer_s = 60;
    last_water_quality = 0xFF;

    last_temp_state = 0xFF;
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
    static uint8_t req_time_timer = 0;
    uint8_t is_temp_high;
    uint8_t current_temp;

    if (mcu_get_wifi_work_state() != WIFI_CONN_CLOUD) {
        return;
    }

    /* 尚未取得时间时，每 3 秒提交一次校时请求。 */
    if (g_Device.current_unix_time == 0) {
        req_time_timer++;
        if (req_time_timer >= 3) {
            req_time_timer = 0;
            App_Wifi_TimeSyncSubmit();
        }
    } else {
        req_time_timer = 0;
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
        if (SUCCESS == mcu_dp_bool_update(DPID_SWITCH,
                                         (g_Device.state != STATE_OFF))) {
            g_Device_Shadow.state = g_Device.state;
        }
    }

    /* 内部模式 0 感应、1 持续；同时同步 WORK_MODE 和旧 PIR/CONTINUE DP。 */
    if (g_Device.target_work_mode != g_Device_Shadow.target_work_mode) {
        if (SUCCESS == mcu_dp_enum_update(
                DPID_WORK_MODE, g_Device.target_work_mode == 1 ? 0 : 1)) {
            mcu_dp_bool_update(DPID_PIR, g_Device.target_work_mode == 0);
            mcu_dp_bool_update(DPID_CONTINUE, g_Device.target_work_mode == 1);
            g_Device_Shadow.target_work_mode = g_Device.target_work_mode;
        }
    }

    /* 缺水报警：累计翻转 10 次或持续缺水 60 秒，成功提交后冷却 1800 秒。 */
    if (last_is_empty == 0xFF) {
        last_is_empty = g_Device.is_empty;
    } else if (g_Device.is_empty != last_is_empty) {
        empty_debounce_count++;
        last_is_empty = g_Device.is_empty;
    }

    if (g_Device.is_empty) {
        if (empty_stable_timer_s < 0xFFFF) {
            empty_stable_timer_s++;
        }
    } else {
        empty_stable_timer_s = 0;
        empty_report_cooldown_s = 0;
        if (g_Device_Shadow.is_empty != 0) {
            if (SUCCESS == mcu_dp_enum_update(DPID_WATER_LEVEL, 1)) {
                g_Device_Shadow.is_empty = 0;
                empty_debounce_count = 0;
            }
        }
    }

    if ((empty_debounce_count >= 10) || (empty_stable_timer_s >= 60)) {
        if (g_Device.is_empty && (empty_report_cooldown_s == 0)) {
            if (SUCCESS == mcu_dp_enum_update(DPID_WATER_LEVEL, 0)) {
                g_Device_Shadow.is_empty = 1;
                empty_report_cooldown_s = 1800;
            }
        }
        empty_debounce_count = 0;
        empty_stable_timer_s = 0;
    }
    if (empty_report_cooldown_s > 0) {
        empty_report_cooldown_s--;
    }

    /* TDS 报警沿用相同的次数、稳定时间和冷却规则；恢复正常时解除冷却并同步。 */
    if (last_water_quality == 0xFF) {
        last_water_quality = g_Device.water_quality;
    } else if (g_Device.water_quality != last_water_quality) {
        tds_debounce_count++;
        last_water_quality = g_Device.water_quality;
    }

    if (g_Device.water_quality == WATER_BAD) {
        if (tds_stable_timer_s < 0xFFFF) {
            tds_stable_timer_s++;
        }
    } else {
        tds_stable_timer_s = 0;
        tds_report_cooldown_s = 0;
        if (g_Device_Shadow.water_quality != g_Device.water_quality) {
            if (SUCCESS == mcu_dp_enum_update(DPID_TDS_RO,
                                             g_Device.water_quality)) {
                g_Device_Shadow.water_quality = g_Device.water_quality;
                tds_debounce_count = 0;
            }
        }
    }

    if ((tds_debounce_count >= 10) || (tds_stable_timer_s >= 60)) {
        if ((g_Device.water_quality == WATER_BAD) &&
            (tds_report_cooldown_s == 0)) {
            if (SUCCESS == mcu_dp_enum_update(DPID_TDS_RO,
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
        if (SUCCESS == mcu_dp_bool_update(DPID_UV, g_Device.uvc_armed)) {
            g_Device_Shadow.uvc_armed = g_Device.uvc_armed;
        }
    }
    if (g_Device.heating_enabled != g_Device_Shadow.heating_enabled) {
        if (SUCCESS == mcu_dp_bool_update(DPID_HEAT, g_Device.heating_enabled)) {
            g_Device_Shadow.heating_enabled = g_Device.heating_enabled;
        }
    }

    /* 温度跨越 >38℃ 的状态边界时立即上报，普通变化每 60 秒检查一次。 */
    current_temp = HAL_ADC_Get_Temperature_C();
    is_temp_high = (current_temp > 38) ? 1 : 0;
    if (last_temp_state == 0xFF) {
        last_temp_state = is_temp_high;
    } else if (is_temp_high != last_temp_state) {
        last_temp_state = is_temp_high;
        if (SUCCESS == mcu_dp_value_update(DPID_TEMP_CURRENT, (u32)current_temp)) {
            last_reported_temp = current_temp;
        }
    }

    temp_sync_timer++;
    if (temp_sync_timer >= 60) {
        if (current_temp != last_reported_temp) {
            if (SUCCESS == mcu_dp_value_update(DPID_TEMP_CURRENT, (u32)current_temp)) {
                last_reported_temp = current_temp;
            }
        }
        temp_sync_timer = 0;
    }

    /* 电量使用 ADC 最终档位：0 高、1 中、2 低；首报、变档及低电量每 1800 秒上报。 */
    {
        uint8_t current_soc;
        current_soc = HAL_ADC_Get_Battery_SOC();

        if (last_bat_soc == 0xFF) {
            if (SUCCESS == mcu_dp_enum_update(DPID_BAT, current_soc)) {
                last_bat_soc = current_soc;
                if (current_soc == 2) {
                    bat_report_cooldown_s = 1800;
                }
            }
        } else if (current_soc != last_bat_soc) {
            if (SUCCESS == mcu_dp_enum_update(DPID_BAT, current_soc)) {
                last_bat_soc = current_soc;
                if (current_soc == 2) {
                    bat_report_cooldown_s = 1800;
                }
            }
        }

        if (current_soc == 2) {
            if (bat_report_cooldown_s == 0) {
                if (SUCCESS == mcu_dp_enum_update(DPID_BAT, current_soc)) {
                    bat_report_cooldown_s = 1800;
                }
            }
        } else {
            bat_report_cooldown_s = 0;
        }
        if (bat_report_cooldown_s > 0) {
            bat_report_cooldown_s--;
        }
    }

    if (g_Device.mute_enabled != g_Device_Shadow.mute_enabled) {
        if (SUCCESS == mcu_dp_bool_update(DPID_BEEP, g_Device.mute_enabled)) {
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
    if (P20) {
        return 1;
    }
    return 0;
}

/* 业务允许休眠且没有输入、负载活动时，累计空闲时间。 */
static void App_Power_IdleTick10ms(void)
{
    if (!App_FSM_StopStateReady() ||
        g_Device.power_mode != POWER_BATTERY ||
        HAL_PWM_Pump_IsRunning() ||
        P02 ||
        HAL_IR_IsActive() ||
        s_power_input_seen) {
        s_power_idle_10ms = 0;
    } else if (s_power_idle_10ms < APP_MCU_STOP_IDLE_10MS) {
        s_power_idle_10ms++;
    }

    s_power_input_seen = 0;
}

/* 入睡前检查空闲时间、外设活动、业务倒计时和待处理请求。 */
static uint8_t App_Power_BaseReady(void)
{
		if (!sc89601_can_stop()) {
			return 0;
		}
	
		/* 允许软件关机，或无显示需求的开机感应待机。 */
		if (!App_FSM_StopStateReady() ||
				!App_Wifi_McuStopReady()) {
				return 0;
		}
		
    if (s_power_idle_10ms < APP_MCU_STOP_IDLE_10MS) {
        return 0;
    }
    if ((g_Device.power_mode != POWER_BATTERY) ||
        HAL_PWM_Pump_IsRunning() || P02 || HAL_IR_IsActive()) {
        return 0;
    }
    if (HAL_Touch_IsBusy() || HAL_Buzzer_IsBusy() || App_Power_InputActive()) {
        return 0;
    }
		if (P10 || P51 ||
				g_Device.timer_pump_s || g_Device.timer_uvc_s ||
				g_Device.display_timer_s || g_Device.wifi_pairing_timer_s) {
				return 0;
		}
    if (g_Device.req_power_toggle || g_Device.req_mode_toggle ||
        g_Device.req_heating || g_Device.req_uvc || g_Device.req_wifi_reset ||
        g_Device.req_cancel_pairing) {
        return 0;
    }
    return 1;
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
            }
        }
    }
}

/* 先结算睡眠 Tick，再恢复 ADC/Timer0；强制采样和 FSM/UI 更新后恢复显示。 */
static void App_Power_Exit(void)
{
    App_Power_ClockTask();
    s_power_sleeping = 0;
    s_power_idle_10ms = 0;
    s_power_adc_div = 0;
    s_power_adc_due = 0;
    s_power_input_seen = 0;

    HAL_Power_TakeInputEvent();  /* g_uvc_exti_flag 留给正常业务消费。 */
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

/* 入睡流程中复查待办事件；保存快照并关闭显示、ADC，保留传感器供电。 */
static void App_Power_TryEnter(void)
{
    if (!App_Power_BaseReady()) {
        return;
    }
    if (flag_10ms || flag_100ms || flag_500ms) {
        return;
    }

    HAL_Power_Prepare();
    if (!App_Power_BaseReady() || flag_10ms || flag_100ms || flag_500ms) {
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

    if (!App_Wifi_McuStopReady() ||App_Power_InputActive() || (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        App_Power_Exit();
        return;
    }
    HAL_Power_StopOnce();
}

/* 每次硬件唤醒后巡检；出现输入或电源变化则退出睡眠，否则喂狗后再次 STOP。 */
static void App_Power_SleepTask(void)
{
    uint8_t event;
    uint16_t battery_mv;
    uint16_t change_mv;

    App_Power_ClockTask();
/* 耗材到期时恢复正常调度，交给原秒任务结算。 */
if (App_FSM_ConsumableUpdateDue()) {
    App_Power_Exit();
    return;
}
    event = HAL_Power_TakeInputEvent();
    if (event || !App_Wifi_McuStopReady() || App_Power_InputActive() || (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        App_Power_Exit();
        return;
    }

    /* 每 500ms 只巡检电池和 Type-C；采样失败、低压或电压变化达到阈值时唤醒。 */
    if (s_power_adc_due) {
        s_power_adc_due = 0;
        WDT_SetReload();
				if (sc89601_sleep_wake_required()) {
						App_Power_Exit();
						return;
				}
        if (!HAL_ADC_PowerPoll(&battery_mv)) {
            App_Power_Exit();
            return;
        }
        if (battery_mv >= s_power_battery_before) {
            change_mv = battery_mv - s_power_battery_before;
        } else {
            change_mv = s_power_battery_before - battery_mv;
        }
				if (HAL_Power_TypeCIsPresent() ||
						(battery_mv < POWER_BAT_LOW_MV) ||
						(change_mv >= POWER_BAT_CHANGE_MV)) {
            App_Power_Exit();
            return;
        }
    }

    if (!App_Wifi_McuStopReady() || App_Power_InputActive() || (P01 != s_power_water_before) ||
        (P03 != s_power_bat_io_before)) {
        App_Power_Exit();
        return;
    }
    WDT_SetReload();
    HAL_Power_StopOnce();
}

#if APP_CHARGE_DEBUG

/* 在100ms任务中调用，约每5秒输出一次。 */
static void App_Charge_Debug100ms(void)
{
    static sc_u8 divider = 0u;

    static const sc_u8 code reg_list[8] = {
        0x00u, 0x01u, 0x02u, 0x05u,
        0x06u, 0x08u, 0x0Au, 0x0Du
    };

    sc_u8 values[8];
    sc_u8 index;
    sc_u8 result;

    if (++divider < 50u) {
        return;
    }
    divider = 0u;

    for (index = 0u; index < 8u; index++) {
        result = sc89601_read_reg(
            reg_list[index], &values[index]);

        if (result != SC89601_OK) {
            printf(
                "\r\n[CHGR] read R%02X failed, ret=%u\r\n",
                (unsigned int)reg_list[index],
                (unsigned int)result);
            return;
        }
    }

    printf(
        "\r\n[CHGR] init=%u poll=%u otg=%u p50=%u p03=%u",
        (unsigned int)g_sc89601_init_result,
        (unsigned int)g_sc89601_poll_result,
        (unsigned int)sc89601_otg_is_active(),
        (unsigned int)P50,
        (unsigned int)P03);

    for (index = 0u; index < 8u; index++) {
        printf(
            " R%02X=%02X",
            (unsigned int)reg_list[index],
            (unsigned int)values[index]);
    }

    printf("\r\n");
}

#endif

/* 初始化硬件、协议和时基，按时间片调度业务。 */
void main(void)
{
    volatile uint32_t boot_delay_i = 300000;
    volatile uint32_t boot_delay_j = 300000;

    while (boot_delay_i--);

    IcResourceInit();
    WiFi_WakeupPin_Init();

#if defined(HIBERNATE_ENABLE)
    g_wifi_allow_sleep = 0;
    g_wifi_lp_state = WIFI_LP_UNKNOWN;
    g_wifi_sleep_reason = 0xFF;
#endif

    /* 保持原有初始化顺序和上电等待。 */
    HAL_LPD_Init();
    P00 = 0;
    HAL_UART_Init();
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
            continue;
        }

        if (HAL_Power_TakeInputEvent()) {
            s_power_input_seen = 1;
        }

        wifi_uart_service();

        /* 暂保留事件清除，后续与协议层诊断代码一起删除。 */
        if (g_wifi_reset_dbg_pending) {
            g_wifi_reset_dbg_pending = 0;
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

			/*
			 * 电压尚未采样或电量为低档时，不启动升压。
			 * 不检查STATE_OFF：关机状态也保留电池升压。
			 */
			sc89601_set_otg_allowed(
					(HAL_ADC_Get_SystemVoltage_mV() != 0u) &&
					(HAL_ADC_Get_Battery_SOC() < 2u));

			sc89601_service_100ms();
#if APP_CHARGE_DEBUG
App_Charge_Debug100ms();
#endif
            App_FSM_Run();
            App_UI_Refresh_Task();
            HAL_PWM_Pump_Constant_Voltage(
                g_Device.sys_voltage_mv);

            if (s_power_display_restore) {
                HAL_TM1650_PowerOn();
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

        App_WifiTest_Task();
        WDT_SetReload();

#if APP_MCU_STOP_ENABLE
        App_Power_TryEnter();
#endif
    }
}