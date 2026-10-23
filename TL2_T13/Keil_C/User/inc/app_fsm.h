#ifndef __APP_FSM_H__
#define __APP_FSM_H__

#include "SC_Init.h"	// MCU initialization header file, including all firmware library header files

// ==========================================
// 耗材默认报警周期 (天)
// ==========================================
#define DEFAULT_CLEAN_DAYS   7
#define DEFAULT_FILTER_DAYS  14

#define APP_CHARGE_NONE     0U  /* 未充电 */
#define APP_CHARGE_ACTIVE   1U  /* 充电中，包含补充充电 */
#define APP_CHARGE_DONE     2U  /* 充电完成 */
#define APP_CHARGE_UNKNOWN  3U  /* 状态未确认 */

uint8_t App_FSM_GetChargeState(void);

// ==========================================
// 枚举类型定义
// ==========================================
typedef enum {
    STATE_OFF = 0,
    STATE_IDLE,
    STATE_SENSING,
    STATE_CONTINUOUS,
    STATE_ERROR
} WorkState_e;

typedef enum {
    POWER_AC = 0,
    POWER_BATTERY
} PowerMode_e;

typedef enum {
    WATER_GOOD = 0,
    WATER_BAD
} WaterQuality_e;

// ==========================================
// 全局数据总线结构体 (核心数据字典)
// ==========================================
typedef struct {
    // --- 真实物理状态 / 意图 ---
    WorkState_e state;
    PowerMode_e power_mode;
    uint8_t     target_work_mode; // 0:感应, 1:持续
    
    // --- 传感器数据 ---
    uint16_t    sys_voltage_mv;
    uint8_t     is_empty;         // 0:有水, 1:缺水
    uint8_t     water_quality;    // 0:优, 1:劣
    
    // --- 硬件开关标志 ---
    uint8_t     heating_enabled;
    uint8_t     uvc_armed;
    uint8_t     mute_enabled;
    
    // --- 中断/协议层请求标志 (单向写入触发标志) ---
    uint8_t     ir_triggered;
    uint8_t     req_power_toggle;
    uint8_t     req_mode_toggle;
    uint8_t     req_heating;
    uint8_t     req_uvc;
    uint8_t     req_wifi_reset;
    uint8_t     req_cancel_pairing; // 【新增】双击取消配网请求标志
        uint8_t  user_cancelled_pairing; // 【新增】：1表示用户双击取消了配网UI，屏蔽兜底逻辑
		// 【新增】：配网超时倒计时 (秒)
    uint16_t wifi_pairing_timer_s;
		
    // --- 软定时器 ---
    uint16_t    timer_pump_s;
    uint16_t    timer_uvc_s;
    uint16_t    display_timer_s;
		
		
		// ======== 清洁报警专用变量 ========
    unsigned int  target_clean_days;     // 目标清洗天数 (App下发，需要存EEPROM)
    unsigned long current_unix_time;     // 当前系统绝对时间 (秒)
    unsigned long last_clean_timestamp;  // 上次点击重置时的绝对时间 (需要存EEPROM)
    unsigned char req_clean_alarm;       // 清洁报警标志位 (1:需要清洗)
		
		// ======== 滤芯寿命报警专用变量 (新增) ========
    unsigned int  target_filter_days;    // 目标滤芯天数 (由 DPID_FILTER_LIFE 下发)
    unsigned long last_filter_timestamp; // 上次更换滤芯的绝对时间 
    unsigned char req_filter_alarm;      // 滤芯报警标志位 (1:需要更换滤芯)
} DeviceStatus_t;

// ==========================================
// 全局变量声明 (实现在 app_fsm.c 中)
// ==========================================
extern volatile DeviceStatus_t g_Device;         // 核心大脑：设备的真实物理状态
extern volatile DeviceStatus_t g_Device_Shadow;  // 影子大脑：记录上一次发给云端的同步状态
extern volatile uint8_t g_cloud_data_synced;
// 时间戳转换函数声明
extern unsigned long Convert_To_Relative_Sec(unsigned char year_since_2000, unsigned char month, unsigned char day, unsigned char hour, unsigned char minute, unsigned char second);
// ==========================================
// 任务与功能接口声明
// ==========================================
void App_FSM_Init(void);
void App_FSM_Run(void);
void App_FSM_Tick_1s(void);

void App_Restore_Factory_Settings(void);//恢复出厂设置（清除本地用户数据并保存）

// 架构重构新增的两个独立任务接口
void App_UI_Refresh_Task(void);         // 纯 UI 渲染任务 (只读)
void App_Cloud_Sync_Task(void);         // 云端影子同步任务 (找茬上报)
void App_FSM_Consumable_Tick_1s(void);

void App_FSM_Pairing_Tick_1s(void);
uint8_t App_FSM_StopStateReady(void);
uint8_t App_FSM_ConsumableUpdateDue(void);
uint8_t App_FSM_BatteryPumpAllowed(void);

uint8_t App_FSM_BatteryShutdownActive(void);
void App_FSM_BatteryTick_1s(void);
#endif // __APP_FSM_H__