#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "tuya_type.h"

/* 0: App wake test; 1: one automatic wake after about 30 seconds. */
#define WIFI_TEST_AUTO_WAKE  0

/* Main-loop context only. Init is for startup, not each wake-up. */
void App_WifiTest_Init(void);
void App_WifiTest_Tick1s(void);
void App_WifiTest_Task(void);
void App_WifiTest_OnDpReceived(void);
/* 标记需要上报；实际发送时读取最新剩余天数 */
void App_Wifi_CleanDaysSubmit(void);
void App_Wifi_FilterDaysSubmit(void);

void App_Wifi_WaterTick1s(void);
void App_Wifi_WaterSyncSubmit(void);

/* Save the latest mute state; no UART transmission or cloud confirmation. */
void App_Wifi_MuteSubmit(u8 value);

/* Save a switch report value, not an instruction to change device power. */
void App_Wifi_SwitchSubmit(u8 value);
/* 旧上报入口：确认本地整帧发送结果，失败转交现有待发服务。 */
u8 App_Wifi_ReportBool(u8 dpid, u8 value);
u8 App_Wifi_ReportEnum(u8 dpid, u8 value);
u8 App_Wifi_ReportValue(u8 dpid, u32 value);
void App_Wifi_ReportMode(void);


/* Preserve the existing startup-only business/reset gates in main.c. */
u8 App_WifiTest_IsBoot(void);
void App_Wifi_OnRemoteReset(void);
void App_Wifi_TimeSyncSubmit(void);
void App_Wifi_TimeSyncTick1s(void);
void App_Wifi_TimeSyncDone(void);
void App_Wifi_ResetNotifySubmit(void);
void App_Wifi_ResetNotifyResult(u8 result);
u8 App_Wifi_McuStopReady(void);
/* 新工作状态帧的 ACK 本地发送完成后调用。 */
void App_Wifi_OnWorkStateReady(void);

#endif
