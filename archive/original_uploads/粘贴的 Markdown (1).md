我接下来发你程序代码，你只需要存入资料库pet文件夹中就好#include "hal_power.h" \
\#include \<intrins.h>     /\* Keil C51：\_nop\_() \*/ \
&#x20;\
/\* \
&#x20;\* 当前Timer0配置： \
&#x20;\* 系统时钟：32MHz \
&#x20;\* Timer0时钟：FSYS / 1 \
&#x20;\* 计数方式：16位模式1 \
&#x20;\* 中断周期：1ms \
&#x20;\* \
&#x20;\* 重装值 = 65536 - 32000 = 33536 \
&#x20;\* \
&#x20;\* 后续若修改系统时钟或Timer0分频， \
&#x20;\* 这里必须与SC_TIM0_Init()同步修改。 \
&#x20;\*/ \
\#define POWER_TIM0_RELOAD       33536U \
&#x20;\
/\* \
&#x20;\* IE1中断使能位： \
&#x20;\* bit2：EBTM \
&#x20;\* bit3：EINT2 \
&#x20;\*/ \
\#define POWER_IE1_EBTM          0x04 \
\#define POWER_IE1_EINT2         0x08 \
&#x20;\
/\* \
&#x20;\* BTMCON： \
&#x20;\* bit7：ENBTM，启动BTM \
&#x20;\* bit6：BTMIF，中断标志 \
&#x20;\* bit3\~0：周期选择 \
&#x20;\* 0011：125ms \
&#x20;\*/ \
\#define POWER_BTM_ENABLE        0x80 \
\#define POWER_BTM_PERIOD_125MS  0x03 \
&#x20;\
///\* \
// \* INT2输入选择： \
// \* \
// \* bit2：INT22，对应P06 \
// \* bit3：INT23，对应P07 \
// \* bit5：INT25，对应P21 \
// \*/ \
//#define POWER_INT2_INPUT_MASK   0x2C \
///\* P06、P21：高有效，使用上升沿 \*/ \
//#define POWER_INT2_RISE_MASK    0x24 \
///\* P07：低有效，使用下降沿 \*/ \
//#define POWER_INT2_FALL_MASK    0x08 \
&#x20;\
/\* \
&#x20;\* 本模块管理 INT22～INT25： \
&#x20;\* P0.6：红外输入，上升沿 \
&#x20;\* P0.7：主触摸输入，下降沿 \
&#x20;\* P2.0：Wi-Fi唤醒输入，上升沿 \
&#x20;\* P2.1：Wi-Fi控制输出，关闭其外部中断边沿 \
&#x20;\*/ \
\#define POWER_INT2_INPUT_MASK   0x3Cu \
\#define POWER_INT2_RISE_MASK    0x14u \
\#define POWER_INT2_FALL_MASK    0x08u \
&#x20;\
/\* \
&#x20;\* 中断负责增加，主循环负责读取并清空。 \
&#x20;\* 8051是8位CPU，读取16位变量不是一次完成， \
&#x20;\* 因此主循环消费它时需要短暂关闭总中断。 \
&#x20;\*/ \
static volatile uint16_t s_btm_ticks = 0; \
&#x20;\
&#x20;\
/\* \
&#x20;\* 外部输入事件锁存。 \
&#x20;\* 即使输入脉冲已经结束，标志仍保留为1， \
&#x20;\* 直到主循环主动消费。 \
&#x20;\*/ \
static volatile uint8_t s_input_event = 0; \
&#x20;\
&#x20;\
/\* \
&#x20;\* 保存Timer0原来的运行和中断使能状态。 \
&#x20;\* \
&#x20;\* 这些变量只由主循环使用，不需要volatile。 \
&#x20;\*/ \
static uint8_t s_saved_tr0 = 0; \
static uint8_t s_saved_et0 = 0; \
&#x20;\
/\* P5.0：低电平表示 Type-C 已插入。 \*/ \
uint8_t HAL_Power_TypeCIsPresent(void) \
{ \
&#x20;   return (P50 == 0) ? 1 : 0; \
} \
&#x20;\
void HAL_Power_Init(void) \
{ \
&#x20;   bit saved_ea; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 配置多个相关寄存器时，暂时关闭中断。 \
&#x20;    \* 完成后恢复调用前的EA状态。 \
&#x20;    \*/ \
&#x20;   saved_ea = EA; \
&#x20;   EA = 0; \
&#x20;\
&#x20;   s_btm_ticks = 0; \
&#x20;   s_input_event = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 配置INT2三路输入的边沿。 \
&#x20;    \* \
&#x20;    \* 先清除这三路的旧配置，再设置新配置。 \
&#x20;    \* 保留INT20、INT21、INT24的原有配置。 \
&#x20;    \* \
&#x20;    \* INT2R：上升沿使能寄存器 \
&#x20;    \* INT2F：下降沿使能寄存器 \
&#x20;    \*/ \
&#x20;   INT2R = \
&#x20;       (INT2R & (uint8_t)(\~POWER_INT2_INPUT_MASK)) | \
&#x20;       POWER_INT2_RISE_MASK; \
&#x20;\
&#x20;   INT2F = \
&#x20;       (INT2F & (uint8_t)(\~POWER_INT2_INPUT_MASK)) | \
&#x20;       POWER_INT2_FALL_MASK; \
&#x20;\
&#x20;   /\* \
&#x20;    \* BTM配置顺序： \
&#x20;    \* \
&#x20;    \* 1. 关闭BTM中断； \
&#x20;    \* 2. 停止BTM并设置125ms周期； \
&#x20;    \* 3. 开启BTM和INT2中断； \
&#x20;    \* 4. 启动BTM。 \
&#x20;    \*/ \
&#x20;\
&#x20;   IE1 &= (uint8_t)(\~POWER_IE1_EBTM); \
&#x20;\
&#x20;   /\* \
&#x20;    \* ENBTM=0：停止 \
&#x20;    \* BTMIF=0：清除旧标志 \
&#x20;    \* BTMFS=0011：125ms \
&#x20;    \*/ \
&#x20;   BTMCON = POWER_BTM_PERIOD_125MS; \
&#x20;\
&#x20;   IE1 |= POWER_IE1_EBTM | POWER_IE1_EINT2; \
&#x20;\
&#x20;   BTMCON |= POWER_BTM_ENABLE; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 不在每次STOP前重启BTM。 \
&#x20;    \* \
&#x20;    \* BTM持续运行，才能让低频节拍包含 \
&#x20;    \* MCU短暂醒着巡检的时间。 \
&#x20;    \*/ \
&#x20;   EA = saved_ea; \
} \
&#x20;\
&#x20;\
/\* \
&#x20;\* ========================================================== \
&#x20;\* 中断侧接口 \
&#x20;\* ========================================================== \
&#x20;\*/ \
&#x20;\
void HAL_Power_BTM_ISR(void) \
{ \
&#x20;   /\* \
&#x20;    \* SC92L7533手册规定： \
&#x20;    \* CPU响应BTM中断后，BTMIF由硬件自动清除。 \
&#x20;    \* \
&#x20;    \* 这里只记录节拍，不执行ADC、UI或状态机。 \
&#x20;    \*/ \
&#x20;   s_btm_ticks++; \
} \
&#x20;\
&#x20;\
void HAL_Power_Input_ISR(void) \
{ \
&#x20;   /\* \
&#x20;    \* 锁存事件。 \
&#x20;    \* \
&#x20;    \* 此处不直接置g_Device.ir_triggered， \
&#x20;    \* 因为同一个INT2入口还包含主触摸。 \
&#x20;    \* \
&#x20;    \* 醒后交给正常扫描完成来源判断和消抖。 \
&#x20;    \*/ \
&#x20;   s_input_event = 1; \
} \
&#x20;\
&#x20;\
/\* \
&#x20;\* ========================================================== \
&#x20;\* 主循环消费接口 \
&#x20;\* ========================================================== \
&#x20;\*/ \
&#x20;\
uint16_t HAL_Power_TakeTicks(void) \
{ \
&#x20;   uint16_t ticks; \
&#x20;   bit saved_ea; \
&#x20;\
&#x20;   saved_ea = EA; \
&#x20;   EA = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 读取与清零放在同一小段临界区。 \
&#x20;    \* 避免中断在读取后、清零前增加的新节拍被覆盖。 \
&#x20;    \*/ \
&#x20;   ticks = s_btm_ticks; \
&#x20;   s_btm_ticks = 0; \
&#x20;\
&#x20;   EA = saved_ea; \
&#x20;\
&#x20;   return ticks; \
} \
&#x20;\
&#x20;\
uint8_t HAL_Power_TakeInputEvent(void) \
{ \
&#x20;   uint8_t event; \
&#x20;   bit saved_ea; \
&#x20;\
&#x20;   saved_ea = EA; \
&#x20;   EA = 0; \
&#x20;\
&#x20;   event = s_input_event; \
&#x20;   s_input_event = 0; \
&#x20;\
&#x20;   EA = saved_ea; \
&#x20;\
&#x20;   return event; \
} \
&#x20;\
&#x20;\
uint8_t HAL_Power_InputPending(void) \
{ \
&#x20;   /\* \
&#x20;    \* 仅查看，不消费。 \
&#x20;    \* 8位标志不需要16位变量那样的组合读取。 \
&#x20;    \*/ \
&#x20;   return s_input_event; \
} \
&#x20;\
&#x20;\
/\* \
&#x20;\* ========================================================== \
&#x20;\* 暂停正常调度 \
&#x20;\* ========================================================== \
&#x20;\*/ \
&#x20;\
void HAL_Power_Prepare(void) \
{ \
&#x20;   bit saved_ea; \
&#x20;\
&#x20;   saved_ea = EA; \
&#x20;   EA = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 保存状态。 \
&#x20;    \* 因此Prepare不能在尚未Resume时再次调用， \
&#x20;    \* 否则会用“已停止”的状态覆盖原来的状态。 \
&#x20;    \*/ \
&#x20;   s_saved_tr0 = TR0; \
&#x20;   s_saved_et0 = ET0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 顺序： \
&#x20;    \* 关闭Timer0中断 → 停止计数 → 清除旧溢出标志。 \
&#x20;    \*/ \
&#x20;   ET0 = 0; \
&#x20;   TR0 = 0; \
&#x20;   TF0 = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* EA恢复后，BTM及外部中断仍能运行。 \
&#x20;    \* \
&#x20;    \* Timer0停止后： \
&#x20;    \* - 不再产生正常毫秒时间片； \
&#x20;    \* - 触摸扫描暂停； \
&#x20;    \* - 蜂鸣器毫秒状态机暂停。 \
&#x20;    \* \
&#x20;    \* 所以应用层必须先确认蜂鸣器空闲、 \
&#x20;    \* 触摸操作已经结算。 \
&#x20;    \*/ \
&#x20;   EA = saved_ea; \
} \
&#x20;\
&#x20;\
/\* \
&#x20;\* ========================================================== \
&#x20;\* 恢复正常调度 \
&#x20;\* ========================================================== \
&#x20;\*/ \
&#x20;\
void HAL_Power_Resume(void) \
{ \
&#x20;   bit saved_ea; \
&#x20;\
&#x20;   saved_ea = EA; \
&#x20;   EA = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 从新的1ms周期开始计时。 \
&#x20;    \* 不沿用睡前剩余的半个Timer0周期。 \
&#x20;    \*/ \
&#x20;   TIM0_Mode1SetReloadCounter(POWER_TIM0_RELOAD); \
&#x20;\
&#x20;   TF0 = 0; \
&#x20;\
&#x20;   /\* \
&#x20;    \* 恢复原来的Timer0使能状态。 \
&#x20;    \* 当前工程正常情况下： \
&#x20;    \* s_saved_et0=1，s_saved_tr0=1。 \
&#x20;    \*/ \
&#x20;   ET0 = s_saved_et0; \
&#x20;   TR0 = s_saved_tr0; \
&#x20;\
&#x20;   EA = saved_ea; \
} \
&#x20;\
&#x20;\
/\* \
&#x20;\* ========================================================== \
&#x20;\* 执行一次STOP \
&#x20;\* ========================================================== \
&#x20;\*/ \
&#x20;\
/\* 执行一次STOP；返回后由应用层决定巡检或恢复正常调度。 \*/ \
void HAL_Power_StopOnce(void) \
{ \
&#x20;   if (!EA) { \
&#x20;       return; \
&#x20;   } \
&#x20;\
&#x20;   if (s_input_event || s_btm_ticks) { \
&#x20;       return; \
&#x20;   } \
&#x20;\
&#x20;   WDT_SetReload(); \
&#x20;   PCON |= 0x02; \
&#x20;\
&#x20;   /\* 保留STOP之后紧接的8条NOP。 \*/ \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
&#x20;   \_nop\_(); \
}#include "sc92f_lpd.h" \
\#include "hal_eeprom.h" \
\#include "hal_lpd.h" \
&#x20;\
/\*\* \
&#x20;\* @brief LPD掉电检测初始化 \
&#x20;\*/ \
void HAL_LPD_Init(void) \
{ \
&#x20;   LPD_DeInit(); // 复位寄存器 \
&#x20;    \
&#x20;   // 1. 设置门限阈值：当 VDD 跌破 2.85V 时触发 \
&#x20;   // 注意：如果你的系统是电池直供且电压会低于2.85V，这里必须调低！ \
&#x20;   LPD_VtripConfig(LPD_VTRIP_2_85V);  \
&#x20;    \
&#x20;   // 2. 开启 LPD 中断，并设置为高优先级 (抢占所有普通业务) \
&#x20;   LPD_ITConfig(ENABLE, HIGH); \
&#x20;    \
&#x20;   // 3. 使能 LPD 模块 \
&#x20;   LPD_Cmd(ENABLE); \
} \
\#include "hal_eeprom.h" \
\#include "HeadFiles/SysFunVarDefine.h"  \
\#include "app_fsm.h"    \
&#x20;\
extern unsigned char IapWriteFlag;  \
&#x20;\
/\*\* \
&#x20;\* @brief 将【清洗+滤芯】数据打包保存到单片机 EEPROM \
&#x20;\*/ \
void HAL_EEPROM_Save_AllData(void) \
{ \
&#x20;   uint8_t write_buf[16]; // 精简为 16 字节 \
&#x20;    \
&#x20;   // --- 打包清洗数据 (前 6 字节) --- \
&#x20;   write_buf[0] = (uint8_t)(g_Device.target_clean_days >> 8); \
&#x20;   write_buf[1] = (uint8_t)(g_Device.target_clean_days & 0xFF); \
&#x20;   write_buf[2] = (uint8_t)(g_Device.last_clean_timestamp >> 24); \
&#x20;   write_buf[3] = (uint8_t)(g_Device.last_clean_timestamp >> 16); \
&#x20;   write_buf[4] = (uint8_t)(g_Device.last_clean_timestamp >> 8); \
&#x20;   write_buf[5] = (uint8_t)(g_Device.last_clean_timestamp & 0xFF); \
&#x20;    \
&#x20;   // --- 打包滤芯数据 (中间 6 字节) --- \
&#x20;   write_buf[6] = (uint8_t)(g_Device.target_filter_days >> 8); \
&#x20;   write_buf[7] = (uint8_t)(g_Device.target_filter_days & 0xFF); \
&#x20;   write_buf[8] = (uint8_t)(g_Device.last_filter_timestamp >> 24); \
&#x20;   write_buf[9] = (uint8_t)(g_Device.last_filter_timestamp >> 16); \
&#x20;   write_buf[10]= (uint8_t)(g_Device.last_filter_timestamp >> 8); \
&#x20;   write_buf[11]= (uint8_t)(g_Device.last_filter_timestamp & 0xFF); \
&#x20;\
&#x9;	/\* A6 表示包含报警标志的新格式。 \*/ \
&#x9;	write_buf[12] = 0x5A; \
&#x9;	write_buf[13] = 0xA6; \
&#x9;	write_buf[14] = g_Device.req_clean_alarm ? 1 : 0; \
&#x9;	write_buf[15] = g_Device.req_filter_alarm ? 1 : 0; \
&#x20;\
&#x20;   EA = 0; // 关闭全局中断 \
&#x20;\
&#x20;   IapWriteFlag = 0x69;  \
&#x20;   IAP_SectorErase(IAP_MEMTYPE_EEPROM, EEPROM_DATA_ADDR, EEPROM_TIME_LIMIT); \
&#x20;    \
&#x20;   IapWriteFlag = 0x69; \
&#x20;   IAP_ProgramByteArray(EEPROM_DATA_ADDR, write_buf, 16, IAP_MEMTYPE_EEPROM, EEPROM_TIME_LIMIT); \
&#x20;    \
&#x20;   IapWriteFlag = 0x00;  \
&#x20;    \
&#x20;   EA = 1; // 恢复全局中断 \
} \
&#x20;\
/\*\* \
&#x20;\* @brief 开机统一读取【清洗+滤芯】数据 \
&#x20;\*/ \
void HAL_EEPROM_Load_AllData(void) \
{ \
&#x20;   uint8_t read_buf[16];  \
&#x20;   unsigned char need_save = 0;  \
&#x20;\
&#x20;   IAP_ReadByteArray(EEPROM_DATA_ADDR, read_buf, 16, IAP_MEMTYPE_EEPROM); \
&#x20;\
&#x20;   // 校验魔术字 \
&#x9;	/\* A5 是旧格式，A6 是新增报警标志的格式。 \*/ \
&#x9;	if (read_buf[12] != 0x5A || \
&#x9;			(read_buf[13] != 0xA5 && read_buf[13] != 0xA6)) { \
&#x9;			App_Restore_Factory_Settings(); \
&#x9;			return; \
&#x9;	} \
&#x20;    \
&#x20;   // --- 解析清洗数据 --- \
&#x20;   g_Device.target_clean_days = ((unsigned int)read_buf[0] << 8) | read_buf[1]; \
&#x20;   g_Device.last_clean_timestamp = ((unsigned long)read_buf[2] << 24) |  \
&#x20;                                   ((unsigned long)read_buf[3] << 16) |  \
&#x20;                                   ((unsigned long)read_buf[4] << 8)  |  \
&#x20;                                   read_buf[5]; \
&#x20;                                    \
&#x20;   // --- 解析滤芯数据 --- \
&#x20;   g_Device.target_filter_days = ((unsigned int)read_buf[6] << 8) | read_buf[7]; \
&#x20;   g_Device.last_filter_timestamp = ((unsigned long)read_buf[8] << 24) |  \
&#x20;                                    ((unsigned long)read_buf[9] << 16) |  \
&#x20;                                    ((unsigned long)read_buf[10] << 8) |  \
&#x20;                                    read_buf[11]; \
&#x20;\
&#x9;	/\* 0 天是合法状态；大于 365 也包含未初始化的 0xFFFF。 \*/ \
&#x9;	if (g_Device.target_clean_days > 365U) { \
&#x9;			g_Device.target_clean_days = DEFAULT_CLEAN_DAYS; \
&#x9;			g_Device.last_clean_timestamp = 0; \
&#x9;			need_save = 1; \
&#x9;	} \
&#x20;\
&#x9;	if (g_Device.target_filter_days > 365U) { \
&#x9;			g_Device.target_filter_days = DEFAULT_FILTER_DAYS; \
&#x9;			g_Device.last_filter_timestamp = 0; \
&#x9;			need_save = 1; \
&#x9;	} \
&#x20;    \
&#x9;	/\* 旧记录没有报警信息；新记录恢复保存的到期状态。 \*/ \
&#x9;	g_Device.req_clean_alarm = 0; \
&#x9;	g_Device.req_filter_alarm = 0; \
&#x20;\
&#x9;	if (read_buf[13] == 0xA6) { \
&#x9;			if (g_Device.target_clean_days == 0 && \
&#x9;					read_buf[14] == 1) { \
&#x9;					g_Device.req_clean_alarm = 1; \
&#x9;			} \
&#x20;\
&#x9;			if (g_Device.target_filter_days == 0 && \
&#x9;					read_buf[15] == 1) { \
&#x9;					g_Device.req_filter_alarm = 1; \
&#x9;			} \
&#x9;	} \
&#x20;\
&#x20;   if (need_save) { \
&#x20;       HAL_EEPROM_Save_AllData(); \
&#x20;   } \
}#include "hal_uart.h" \
\#include "SC_Init.h" \
\#include \<stdio.h> \
&#x20;\
// 1. 增加一个 USCI2 专用的软件发送完成标志位 \
volatile bit g_usci2_tx_done = 1;  \
&#x20;\
/\* \
&#x20;\* 0：本次发送尝试尚未记录底层错误 \
&#x20;\* 1：全局中断关闭 \
&#x20;\* 2：开始发送时，上一字节尚未完成 \
&#x20;\* 3：等待发送完成超时 \
&#x20;\*/ \
static unsigned char s_usci2_tx_fault_reason = 0; \
&#x20;\
unsigned char WiFi_UART_GetTxFaultReason(void) \
{ \
&#x20;   return s_usci2_tx_fault_reason; \
} \
&#x20;\
/\* 一旦发送异常，保持置位，避免继续发送损坏的协议帧 \*/ \
static bit s_usci2_tx_fault = 0; \
&#x20;\
unsigned char WiFi_UART_HasTxFault(void) \
{ \
&#x20;   return s_usci2_tx_fault ? 1 : 0; \
} \
&#x20;\
void HAL_UART_Init(void) { \
&#x20;   // 调用赛元生成的底层初始化 \
&#x20;   SC_UART0_Init(); \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  重定向 C 语言标准库的 putchar 函数 \
&#x20;\* @note   有了这个函数，你就可以在任何地方直接使用 printf() 了！ \
&#x20;\*/ \
char putchar(char c) { \
&#x9;    uint16_t timeout = 5000; \
&#x20;\
&#x20;   // 将单个字符塞入串口发送寄存器 \
&#x20;   SBUF = c; \
&#x20;    \
&#x20;   // 等待单字节发送完成，带超时兜底（TI 是 8051 内部的发送完成标志位） \
&#x20;   while (!TI) { \
&#x20;       if (--timeout == 0) break; \
&#x20;   } \
&#x20;\
&#x20;   // 软件手动清零，准备发送下一个字符 \
&#x20;   TI = 0;       \
&#x20;    \
&#x20;   return c; \
} \
&#x20;\
/\* \
&#x20;\* 仅在主循环上下文调用。 \
&#x20;\* 调用期间必须允许 USCI2 发送中断执行。 \
&#x20;\* printf 使用独立的 UART0 调试串口。 \
&#x20;\*/ \
void WiFi_UART_SendByte(unsigned char value) \
{ \
&#x20;   uint16_t timeout = 5000; \
&#x20;   static unsigned char s_first_timeout_printed = 0; \
&#x20;\
&#x20;   /\* 本帧已经出现发送故障，不继续发送剩余字节 \*/ \
&#x20;   if (s_usci2_tx_fault) { \
&#x20;       return; \
&#x20;   } \
&#x20;\
&#x20;   /\* 全局中断关闭，不能启动依赖中断完成的发送 \*/ \
&#x20;   if (!EA) { \
&#x20;       s_usci2_tx_fault_reason = 1; \
&#x20;       s_usci2_tx_fault = 1; \
&#x20;       return; \
&#x20;   } \
&#x20;\
&#x20;   /\* 上一字节尚未完成，不覆盖发送寄存器 \*/ \
&#x20;   if (!g_usci2_tx_done) { \
&#x20;       s_usci2_tx_fault_reason = 2; \
&#x20;       s_usci2_tx_fault = 1; \
&#x20;       return; \
&#x20;   } \
&#x20;\
//    USCI2_ClearFlag(USCI2_UART_FLAG_TI); \
/\* 仅清 TI，保留可能同时到达的 RI。 \*/ \
US2CON0 = (US2CON0 & 0xFC) | 0x02; \
&#x9;	 \
&#x20;   g_usci2_tx_done = 0; \
&#x20;   USCI2_UART_SendData8(value); \
&#x20;\
&#x20;   /\* 防卡死计数，5000 不是毫秒数 \*/ \
&#x20;   while (!g_usci2_tx_done) { \
&#x20;       if (--timeout == 0) { \
&#x20;           break; \
&#x20;       } \
&#x20;   } \
&#x20;\
&#x20;   /\* 等待结束后仍未观察到发送完成，记录故障 \*/ \
&#x20;   if (!g_usci2_tx_done) { \
&#x20;       s_usci2_tx_fault_reason = 3; \
&#x20;       s_usci2_tx_fault = 1; \
&#x20;\
&#x20;       /\* 每次上电只打印第一次发送等待超时 \*/ \
&#x20;       if (!s_first_timeout_printed) { \
&#x20;           s_first_timeout_printed = 1; \
&#x20;\
&#x20;           printf( \
&#x20;               "\r\n[UTX-FIRST] byte=%02X ea=%u done=%u\r\n", \
&#x20;               (unsigned int)value, \
&#x20;               (unsigned int)EA, \
&#x20;               (unsigned int)g_usci2_tx_done); \
&#x20;       } \
&#x20;   } \
} \
&#x20;\
/\* \
&#x20;\* 仅在主循环准备发送一帧新数据时调用。 \
&#x20;\* 不允许在一帧的中途调用。 \
&#x20;\*/ \
unsigned char WiFi_UART_BeginFrame(void) \
{ \
&#x9;    s_usci2_tx_fault_reason = 0; \
&#x20;\
&#x20;   /\* 当前发送依赖中断 \*/ \
&#x20;   if (!EA) { \
&#x9;		        s_usci2_tx_fault_reason = 1; \
&#x20;\
&#x20;       return 0; \
&#x20;   } \
&#x20;\
&#x20;   /\* 上一字节尚未完成，不能启动新帧 \*/ \
&#x20;   if (!g_usci2_tx_done) { \
&#x9;		        s_usci2_tx_fault_reason = 2; \
&#x20;\
&#x20;       return 0; \
&#x20;   } \
&#x20;\
&#x20;   /\* \
&#x20;    \* 上一字节已完成，允许新帧重新尝试。 \
&#x20;    \* 不继续发送上一帧剩余的字节。 \
&#x20;    \*/ \
&#x20;   s_usci2_tx_fault = 0; \
&#x20;\
&#x20;   return 1; \
} \
&#x20;\
/\* \
&#x20;\* 只检查现有发送入口的两个前置条件。 \
&#x20;\* 不清故障、不修改完成标志、不写发送寄存器。 \
&#x20;\*/ \
unsigned char WiFi_UART_IsTxReady(void) \
{ \
&#x20;   if (!EA || !g_usci2_tx_done) { \
&#x20;       return 0; \
&#x20;   } \
&#x20;\
&#x20;   return 1; \
} \
&#x20;\
/\* \
&#x20;\* 初始化 MCU -> Wi-Fi 唤醒脚。 \
&#x20;\* 按当前 wkp=0 配置，低电平保持模组工作。 \
&#x20;\*/ \
void WiFi_WakeupPin_Init(void) \
{ \
&#x9;  /\* MCU -> 模组 P24：低电平保持模组工作 \*/ \
&#x20;   /\* 先设置输出锁存值，再打开输出 \*/ \
&#x20;   P21  = 0; \
&#x20;   GPIO_Init(GPIO2, GPIO_PIN_1, GPIO_MODE_OUT_PP); \
&#x9; \
&#x9;	/\* 模组 P32 -> MCU：高电平唤醒输入 \*/ \
&#x20;   GPIO_Init(GPIO2, GPIO_PIN_0, GPIO_MODE_IN_HI); \
} \
&#x20;\
/\* 请求唤醒，并持续保持模组工作 \*/ \
void WiFi_Module_KeepAwake(void) \
{ \
&#x20;   P21 = 0; \
} \
&#x20;\
/\* 释放保持工作条件，不代表模组已经休眠 \*/ \
void WiFi_Module_Release(void) \
{ \
&#x20;   P21 = 1; \
}#include "hal_touch.h" \
\#include "app_fsm.h" \
\#include "hal_buzzer.h" \
\#include \<stdio.h>  \
\#include "HeadFiles\SysFunVarDefine.h" \
\#include "wifi.h"  // 【新增】：必须引入wifi头文件，才能调用 mcu_reset_wifi() \
&#x20;\
// UVC 外部中断标志位 (保留) \
extern uint8_t g_uvc_exti_flag; \
&#x20;\
// ========================================== \
// 硬件引脚映射 \
// ========================================== \
\#define TOUCH_MAIN_PIN  P07  \
\#define UVCING_PIN      P51  \
&#x20;\
/\* Timer0扫描更新，主循环读取 \*/ \
static volatile uint8_t s_touch_busy = 0; \
&#x20;\
uint8_t HAL_Touch_IsBusy(void) \
{ \
&#x20;   return s_touch_busy; \
} \
&#x20;\
void HAL_Touch_Init(void) { \
&#x20;   // 初始化逻辑（如果没有可留空） \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  触摸按键扫描与指令解析 (被 Timer0Interrupt 每 10ms 调用一次) \
&#x20;\*/ \
void HAL_Touch_Scan(void) { \
&#x20;   // --------------------------------------------------------- \
&#x20;   // 1. 主按键 P07 扫描逻辑 (高灵敏度、无阻塞、支持多击与长按) \
&#x20;   // --------------------------------------------------------- \
&#x20;   static uint16_t key_press_timer = 0; \
&#x20;   static uint16_t key_release_timer = 0; \
&#x20;   static uint8_t  click_count = 0; \
&#x20;   static uint8_t  long_press_3s_done = 0; \
&#x20;   static uint8_t  long_press_5s_done = 0; \
&#x20;   static uint8_t uvc_dead_timer = 0;     \
&#x9;	/\* 记录本次按下时的状态，避免长按期间状态变化触发配网。 \*/ \
&#x9;	static uint8_t key_started_off = 0; \
&#x20;   // 假设按下为低电平 \
&#x20;   uint8_t is_pressed = (TOUCH_MAIN_PIN == 0) ? 1 : 0; \
&#x20;\
&#x9;	    // 【新增核心防护】：P02 断电保护时，强制屏蔽所有按键输入并清空状态缓存 \
&#x20;   // 防止触摸芯片断电后引脚浮空，引发“幽灵按键”导致状态机错乱 \
&#x20;   if (P00 == 0) { \
&#x9;			key_started_off = 0; \
&#x20;       key_press_timer = 0; \
&#x20;       key_release_timer = 0; \
&#x20;       click_count = 0; \
&#x20;       long_press_3s_done = 0; \
&#x20;       long_press_5s_done = 0; \
&#x20;       uvc_dead_timer = 0; \
&#x20;       g_uvc_exti_flag = 0; // 顺便吃掉 UVC 的中断标志 \
&#x9;		    s_touch_busy = 0;       /\* 新增 \*/ \
&#x20;       return; \
&#x20;   } \
&#x9;	 \
&#x20;   if (is_pressed) { \
&#x20;       if (key_press_timer == 0) { \
&#x20;           key_started_off = \
&#x20;               (g_Device.state == STATE_OFF) ? 1 : 0; \
&#x20;       } \
&#x20;\
&#x20;       key_release_timer = 0; \
&#x20;\
&#x20;       /\* 达到最长判定时间后保持，避免持续按住导致计数回绕。 \*/ \
&#x20;       if (key_press_timer < 500U) { \
&#x20;           key_press_timer++; \
&#x20;       } \
&#x20;\
&#x20;       /\* 沿用现有1秒门槛，松手后提交开关机请求。 \*/ \
&#x20;       if (key_press_timer == 100U) { \
&#x20;           long_press_3s_done = 1; \
&#x20;       } \
&#x20;\
&#x20;       /\* 只有开机时开始的长按，才允许在5秒触发配网。 \*/ \
&#x20;       if ((key_press_timer == 500U) && \
&#x20;           !long_press_5s_done && \
&#x20;           !key_started_off && \
&#x20;           (g_Device.state != STATE_OFF)) { \
&#x20;\
&#x20;           g_Device.req_wifi_reset = 1; \
&#x20;\
&#x20;           if (!g_Device.mute_enabled) { \
&#x20;               HAL_Buzzer_Beep_3s(); \
&#x20;           } \
&#x20;\
&#x20;           long_press_5s_done = 1; \
&#x20;       } \
&#x20;   } else { \
&#x20;       // 刚松手瞬间结算 \
&#x20;       /\* 松手后区分短按与长按。 \*/ \
&#x20;       if (key_press_timer > 3) { \
&#x20;           if (!long_press_3s_done && !long_press_5s_done) { \
&#x20;               click_count++; \
&#x20;\
&#x20;               if (click_count == 2) { \
&#x20;                   if (g_Device.state != STATE_OFF) { \
&#x20;                       if (g_Device.wifi_pairing_timer_s > 0) { \
&#x20;                           g_Device.wifi_pairing_timer_s = 0; \
&#x20;                           g_Device.req_cancel_pairing = 1; \
&#x20;\
&#x20;                           if (!g_Device.mute_enabled) { \
&#x20;                               HAL_Buzzer_Beep(2); \
&#x20;                           } \
&#x20;                       } else { \
&#x20;                           g_Device.req_heating = 1; \
&#x20;\
&#x20;                           if (!g_Device.mute_enabled) { \
&#x20;                               HAL_Buzzer_Beep(2); \
&#x20;                           } \
&#x20;                       } \
&#x20;                   } \
&#x20;\
&#x20;                   click_count = 0; \
&#x20;               } \
&#x20;           } else if (long_press_3s_done && \
&#x20;                      !long_press_5s_done) { \
&#x20;               /\* \
&#x20;                \* 关机开始的长按只允许开机； \
&#x20;                \* 按住期间已被APP开机，则不再反向关机。 \
&#x20;                \*/ \
&#x20;               if (!key_started_off || \
&#x20;                   (g_Device.state == STATE_OFF)) { \
&#x20;                   g_Device.req_power_toggle = 1; \
&#x20;               } \
&#x20;           } \
&#x20;       } \
&#x20;       // 清理按压状态 \
&#x20;       if (key_press_timer > 0) { \
&#x20;           key_press_timer = 0;  \
&#x20;           long_press_3s_done = 0; \
&#x20;           long_press_5s_done = 0; \
&#x9;				  key_started_off = 0; \
&#x20;\
&#x20;       } \
&#x20;\
&#x20;       // 【单击结算】：只有当处于单击状态，且持续松手 250ms 后，才判定为单击 \
&#x20;       if (click_count == 1) { \
&#x20;           key_release_timer++; \
&#x20;           if (key_release_timer >= 25) { \
&#x20;               // 【核心修复：关机状态下屏蔽单击意图】 \
&#x20;               if (g_Device.state != STATE_OFF) {  \
&#x20;                   if (g_Device.wifi_pairing_timer_s == 0) { \
&#x20;                       g_Device.req_mode_toggle = 1;  \
&#x20;                   } \
&#x20;               } \
&#x20;               click_count = 0; \
&#x20;               key_release_timer = 0; \
&#x20;           } \
&#x20;       } else { \
&#x20;           key_release_timer = 0; \
&#x20;       } \
&#x20;   } \
&#x20;\
&#x20;   // --------------------------------------------------------- \
&#x20;   // 2. 处理 UVC 按键 (外部中断标志位 + 冷却防抖) \
&#x20;   // ---------------------------------------------------------     \
&#x20;   // 每次进入 10ms 轮询时，倒数冷却时间 \
&#x20;   if (uvc_dead_timer > 0) { \
&#x20;       uvc_dead_timer--;  \
&#x20;   } \
&#x20;\
&#x20;   if (g_Device.state != STATE_OFF) { \
&#x20;       if (g_uvc_exti_flag) { \
&#x20;           // 只有在冷却期结束时，才认为是一次有效的点击 \
&#x20;           if (uvc_dead_timer == 0) { \
&#x20;               g_Device.req_uvc = 1;      \
&#x20;               uvc_dead_timer = 50; // 强制进入 500ms 冷却 (50 \* 10ms) \
&#x20;           }             \
&#x20;           g_uvc_exti_flag = 0; // 吃掉中断标志位 \
&#x20;       } \
&#x20;   } else { \
&#x20;       g_uvc_exti_flag = 0; // 关机状态丢弃 UVC 按键 \
&#x20;   } \
&#x9;	 /\* \
&#x20;    \* 按住、按键结算未完成，或UVC冷却尚未结束， \
&#x20;    \* 都保持正常调度，暂不休眠。 \
&#x20;    \*/ \
&#x20;   s_touch_busy = \
&#x20;       is_pressed || \
&#x20;       (key_press_timer != 0) || \
&#x20;       (click_count != 0) || \
&#x20;       (uvc_dead_timer != 0); \
} \
&#x20;\
// ========================================== \
// UVC驱动区域 \
// ========================================== \
void HAL_UVCING_Start(void) { \
&#x20;   UVCING_PIN = 1; // 打开 MOS 管 \
} \
&#x20;\
void HAL_UVCING_Stop(void) { \
&#x20;   UVCING_PIN = 0; // 关闭 MOS 管 \
}#include "hal_tm1650.h" \
\#include \<intrins.h> // 提供 \_nop\_() 函数 \
&#x20;\
// --- 硬件引脚映射 --- \
// 注意：在 SC_GPIO_Init 中，这两个脚需要配置为准双向口或开漏输出(上拉) \
\#define TM_SCL  P04 \
\#define TM_SDA  P05 \
&#x20;\
// --- TM1650 寄存器地址定义 --- \
\#define TM1650_CMD_SYS   0x48  // 系统控制命令 (显存操作) \
\#define TM1650_DIG1      0x68  // 显存地址1 \
\#define TM1650_DIG2      0x6A  // 显存地址2 \
\#define TM1650_DIG3      0x6C  // 显存地址3 \
\#define TM1650_DIG4      0x6E  // 显存地址4 \
&#x20;\
// --- 显存缓存区 --- \
// 因为 TM1650 不支持单独修改某个位(只能整字节写入)， \
// 我们需要在 RAM 里建一个显存缓存，修改缓存后再把整字节刷进去。 \
static uint8_t TM1650_Buffer[4] = {0, 0, 0, 0}; \
&#x20;\
static void I2C_Delay(void) { \
&#x9;	// ！！！核心：加上 volatile 关键字 ！！！ \
&#x20;   // 这会警告编译器：“绝对不许优化这段代码，必须老老实实执行” \
&#x20;   volatile uint8_t i = 15;  \
//    while(i--); \
} \
static void I2C_Start(void) { \
&#x20;   TM_SDA = 1; \
&#x20;   TM_SCL = 1; \
&#x20;   I2C_Delay(); \
&#x20;   TM_SDA = 0; // SCL高期间，SDA拉低产生起始信号 \
&#x20;   I2C_Delay(); \
&#x20;   TM_SCL = 0; \
&#x9;	I2C_Delay(); \
} \
&#x20;\
static void I2C_Stop(void) { \
&#x20;   TM_SCL = 0;  \
&#x20;   TM_SDA = 0; // 确保SDA在SCL拉高前是低电平 \
&#x20;   I2C_Delay(); \
&#x20;   TM_SCL = 1; \
&#x20;   I2C_Delay(); \
&#x20;   TM_SDA = 1; // SCL高电平时，SDA拉高产生停止信号 \
&#x20;   I2C_Delay(); \
} \
&#x20;\
static void I2C_WriteByte(uint8_t dat) { \
&#x20;   uint8_t i; \
&#x20;   for (i = 0; i < 8; i++) { \
&#x20;       TM_SCL = 0; // 先拉低时钟，允许数据变化 \
&#x20;       I2C_Delay(); \
&#x20;        \
&#x20;       if (dat & 0x80) { \
&#x20;           TM_SDA = 1; \
&#x20;       } else { \
&#x20;           TM_SDA = 0; \
&#x20;       } \
&#x20;       I2C_Delay(); // 等待数据稳定 \
&#x20;        \
&#x20;       TM_SCL = 1; // 拉高时钟，让 TM1650 采样 \
&#x20;       I2C_Delay();  \
&#x20;        \
&#x20;       dat <<= 1; \
&#x20;   } \
&#x20;    \
&#x20;   // --- 极其关键的第 9 个时钟 (处理 ACK) --- \
&#x20;   TM_SCL = 0; // 拉低时钟，准备接收 ACK \
&#x20;   TM_SDA = 1; // ！！！MCU必须释放总线(写1)，允许 TM1650 拉低总线 ！！！ \
&#x20;   I2C_Delay(); \
&#x20;    \
&#x20;   TM_SCL = 1; // 产生第 9 个时钟，TM1650 此时应该拉低了 SDA \
&#x20;   I2C_Delay();  \
&#x20;    \
&#x20;   TM_SCL = 0; // 结束第 9 个时钟 \
&#x20;   I2C_Delay(); \
} \
&#x20;\
// 写入一个指令到 TM1650 \
static void TM1650_WriteCmd(uint8_t addr, uint8_t dat) { \
&#x20;   I2C_Start(); \
&#x20;   I2C_WriteByte(addr); \
&#x20;   I2C_WriteByte(dat); \
&#x20;   I2C_Stop(); \
} \
&#x20;\
&#x20;\
/\*\* \
&#x20;\* @brief  模拟 I2C 读取一个字节 \
&#x20;\* @return 读到的 8 bit 数据 \
&#x20;\*/ \
static uint8_t I2C_ReadByte(void) { \
&#x20;   uint8_t i; \
&#x20;   uint8_t dat = 0; \
&#x20;\
&#x20;   // 重点：单片机准备读取数据前，必须释放 SDA 总线 \
&#x20;   // (在准双向口/开漏模式下，写 1 代表释放总线，配置为输入) \
&#x20;   TM_SDA = 1;  \
&#x20;   I2C_Delay(); \
&#x20;\
&#x20;   for (i = 0; i < 8; i++) { \
&#x20;       dat <<= 1; \
&#x20;        \
&#x20;       TM_SCL = 1; // 时钟拉高，此时 TM1650 会把数据放到 SDA 上 \
&#x20;       I2C_Delay(); \
&#x20;        \
&#x20;       // 采样 SDA 数据 \
&#x20;       if (TM_SDA) { \
&#x20;           dat |= 0x01; \
&#x20;       } \
&#x20;        \
&#x20;       TM_SCL = 0; // 时钟拉低，通知 TM1650 准备下一位 \
&#x20;       I2C_Delay(); \
&#x20;   } \
&#x20;\
&#x20;   // 处理第 9 个时钟 (ACK 时钟位) \
&#x20;   // 根据 TM1650 数据手册图5，读数据时无需主机应答，只需给足时钟信号即可释放 \
&#x20;   TM_SDA = 1;  \
&#x20;   I2C_Delay(); \
&#x20;   TM_SCL = 1; \
&#x20;   I2C_Delay(); \
&#x20;   TM_SCL = 0; \
&#x20;\
&#x20;   return dat; \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  读取 TM1650 当前的按键状态 \
&#x20;\* @return 8位键值。如果没有按键按下，通常返回 0x00 (或其它非按下的释放码) \
&#x20;\*/ \
uint8_t HAL_TM1650_ReadKey(void) { \
&#x20;   uint8_t keycode = 0; \
&#x20;    \
&#x20;   I2C_Start(); \
&#x20;   I2C_WriteByte(0x4F);     // 发送读取按键的命令 0x4F \
&#x20;   keycode = I2C_ReadByte(); // 读取 1 个字节的键值 \
&#x20;   I2C_Stop(); \
&#x20;    \
&#x20;   return keycode; \
} \
&#x20;\
// ========================================== \
// 应用层 API 接口 \
// ========================================== \
&#x20;\
/\*\* \
&#x20;\* @brief  初始化 TM1650 \
&#x20;\*/ \
void HAL_TM1650_Init(void) { \
&#x20;   // 0x31: 开启显示，3级亮度(0x31\~0x37为不同亮度)，7段模式 \
&#x20;   TM1650_WriteCmd(TM1650_CMD_SYS, 0x01); //原为0x31 \
&#x20;    \
//    HAL_LED_ClearAll(); // 上电清空显示 \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  清空所有指示灯 \
&#x20;\*/ \
void HAL_LED_ClearAll(void) { \
&#x20;   uint8_t i; \
&#x20;   for(i=0; i<4; i++) TM1650_Buffer[i] = 0; \
&#x20;    \
&#x20;   TM1650_WriteCmd(TM1650_DIG1, 0x00); \
&#x20;   TM1650_WriteCmd(TM1650_DIG2, 0x00); \
&#x20;   TM1650_WriteCmd(TM1650_DIG3, 0x00); \
&#x20;   TM1650_WriteCmd(TM1650_DIG4, 0x00); \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  控制单颗 LED 的亮灭 (支持分级映射与逻辑反转) \
&#x20;\* @param  led_id: 灯光枚举 ID \
&#x20;\* @param  status: LED_ON 或 LED_OFF \
&#x20;\*/ \
void HAL_LED_SetStatus(LedId_t led_id, uint8_t status) { \
&#x20;   uint8_t addr_index = 0;  // 对应 DIG1\~DIG4 (0\~3) \
&#x20;   uint8_t bit_mask = 0;    // 对应 SEG 的哪一位 \
&#x20;   uint8_t is_inverted = 0; // 标记是否为“相反触发” \
&#x20;   uint8_t final_status = status; // 处理 "相反触发" 的防弹逻辑 \
&#x20;\
&#x20;   // ========================================== \
&#x20;   // 硬件走线严格映射区 (基于原理图 image_403ca0.png) \
&#x20;   // ========================================== \
&#x20;   switch (led_id) { \
&#x20;       // --- DIG2: 状态指示灯 (正向逻辑: 写1亮，写0灭) ---		 \
&#x9;		//===============修改1：指示灯逻辑================================== \
&#x9;		  case LED_NO_WATER:        addr_index = 1; bit_mask = (1 << 1); break; // B1 \
&#x20;       case LED_WATER_Q:         addr_index = 1; bit_mask = (1 << 2); break; // C1 \
&#x20;       case LED_HEATING: 				addr_index = 1; bit_mask = (1 << 3); break; // D1 \
&#x20;       case LED_MODE_CONT:       addr_index = 1; bit_mask = (1 << 4); break; // E1 \
&#x20;       case LED_MODE_SENSE:      addr_index = 1; bit_mask = (1 << 5); break; // F1 \
&#x20;       case LED_SW2:     			  addr_index = 1; bit_mask = (1 << 6); break; // G1 \
&#x20;       case LED_SW1:   					addr_index = 1; bit_mask = (1 << 7); break; // DP1 \
&#x20;\
&#x20;       // --- DIG3: 感应指示灯组 (相反触发: 软件自动翻转) --- \
&#x20;       case LED_SENSE_IND_1: 		addr_index = 2; bit_mask = (1 << 2); break; // C1 \
&#x20;       case LED_SENSE_IND_2: 		addr_index = 2; bit_mask = (1 << 3); break; // D1 \
&#x20;       case LED_SENSE_IND_3: 		addr_index = 2; bit_mask = (1 << 4); break; // E1 \
&#x20;       case LED_SENSE_IND_4: 		addr_index = 2; bit_mask = (1 << 5); break; // F1 \
&#x20;       case LED_SENSE_IND_5: 		addr_index = 2; bit_mask = (1 << 6); break; // G1 \
&#x20;\
&#x20;       default: return; // 非法 ID 保护 \
&#x20;   } \
&#x20;\
&#x20;   // ========================================== \
&#x20;   // 修改显存缓存 (Read-Modify-Write) \
&#x20;   // ========================================== \
&#x20;   if (final_status == LED_ON) { \
&#x20;       TM1650_Buffer[addr_index] |= bit_mask;  // 置 1 \
&#x20;   } else { \
&#x20;       TM1650_Buffer[addr_index] &= \~bit_mask; // 清 0 \
&#x20;   } \
&#x20;    \
&#x20;   // ========================================== \
&#x20;   // 局部刷新：将更新后的单字节推送到芯片，拒绝全局刷屏 \
&#x20;   // ========================================== \
&#x20;   switch (addr_index) { \
&#x20;       case 0: TM1650_WriteCmd(TM1650_DIG1, TM1650_Buffer[0]); break; \
&#x20;       case 1: TM1650_WriteCmd(TM1650_DIG2, TM1650_Buffer[1]); break; \
&#x20;       case 2: TM1650_WriteCmd(TM1650_DIG3, TM1650_Buffer[2]); break; \
&#x20;       case 3: TM1650_WriteCmd(TM1650_DIG4, TM1650_Buffer[3]); break; \
&#x20;   } \
} \
&#x20;\
&#x20;\
/\*\* \
&#x20;\* @brief  将所有 DIG 的 DP 设置为高电平 \
&#x20;\* @note   注意：物理引脚输出的仍是带有消隐间隔的脉冲波，并非纯直流高电平 \
&#x20;\*/ \
void HAL_LED_SetAllSegaHigh1(void) { \
&#x20;   uint8_t i; \
//    uint8_t sega_mask = 0x80; // DP  \
&#x20;       uint8_t sega_mask = 0xff; // ALL  \
&#x20;\
&#x20;   for(i = 0; i < 4; i++) { \
&#x20;       TM1650_Buffer[i] |= sega_mask; // 置 1 \
&#x20;   } \
&#x20;    \
&#x20;   // 2. 将修改后的 4 个字节全部刷新到 TM1650 \
&#x20;   TM1650_WriteCmd(TM1650_DIG1, TM1650_Buffer[0]); \
&#x20;   TM1650_WriteCmd(TM1650_DIG2, TM1650_Buffer[1]); \
&#x20;   TM1650_WriteCmd(TM1650_DIG3, TM1650_Buffer[2]); \
&#x20;   TM1650_WriteCmd(TM1650_DIG4, TM1650_Buffer[3]); \
} \
&#x20;\
&#x20;\
&#x20;\
// ========================================== \
// 应用层 API 接口 (新增电源控制) \
// ========================================== \
&#x20;\
/\*\* \
&#x20;\* @brief  软件关闭 TM1650 显示 (进入低功耗模式) \
&#x20;\* @note   在 P02 物理断电前调用，确保芯片安全下电 \
&#x20;\*/ \
void HAL_TM1650_PowerOff(void) { \
&#x20;   TM1650_WriteCmd(TM1650_CMD_SYS, 0x00); // 0x00: 关闭显示 \
} \
&#x20;\
/\*\* \
&#x20;\* @brief  软件开启 TM1650 显示 \
&#x20;\* @note   通常由 HAL_TM1650_Init() 代替执行 \
&#x20;\*/ \
void HAL_TM1650_PowerOn(void) { \
&#x20;   TM1650_WriteCmd(TM1650_CMD_SYS, 0x01); // 0x01: 开启显示 \
}