/******************************************************************************
 * main.c 负责“策略”：
 *      是否已经空闲60秒？
 *      水泵是否停止？
 *      是否有按键？
 *      是否正在配网？
 *      是否允许睡眠？
 *
 * hal_power.c 负责“机制”：
 *      Timer0 怎么停？
 *      BTM 怎么产生125ms Tick？
 *      INT2 怎么配置？
 *      STOP 怎么进入？
 *      中断事件怎么保存？
 *      Timer0 怎么恢复？
 * ============================================================================
 * 当前低功耗架构
 * ============================================================================
 *
 * 正常运行：
 *
 *      Timer0
 *        │
 *        ├── 1ms基础时钟
 *        ├── Touch
 *        ├── Buzzer
 *        └── 软件任务Flag
 *
 *      BTM
 *        │
 *        └── 每125ms持续产生Tick
 *
 *
 * 准备睡眠：
 *
 *      HAL_Power_Prepare()
 *              │
 *              └── 停止Timer0
 *
 *      main.c关闭：
 *              │
 *              ├── TM1650
 *              └── ADC
 *
 *      HAL_Power_StopOnce()
 *              │
 *              └── PCON.STOP = 1
 *
 *
 * STOP期间：
 *
 *      高频系统时钟停止
 *              │
 *              ├── BTM 125ms → 唤醒
 *              │
 *              └── INT2输入 → 唤醒
 *
 *
 * BTM例行唤醒：
 *
 *      CPU醒来
 *        │
 *        ▼
 *      main.c
 *        │
 *        ▼
 *      App_Power_SleepTask()
 *        │
 *        ├── 无事件 → 再次STOP
 *        │
 *        └── 有事件 → App_Power_Exit()
 *                           │
 *                           ▼
 *                    HAL_Power_Resume()
 *                           │
 *                           ▼
 *                    Timer0恢复运行
 *
 *
 ******************************************************************************/

#include "hal_power.h"
#include <intrins.h>     /* Keil C51：_nop_() */

#define POWER_TIM0_RELOAD       33536U
#define POWER_IE1_EBTM          0x04
#define POWER_IE1_EINT2         0x08
#define POWER_BTM_ENABLE         0x80
#define POWER_BTM_PERIOD_125MS   0x03
#define POWER_INT2_INPUT_MASK   0x3C
#define POWER_INT2_RISE_MASK    0x14
#define POWER_INT2_FALL_MASK    0x08
static volatile uint16_t s_btm_ticks = 0;
static volatile uint8_t s_input_event = 0;
static uint8_t s_saved_tr0 = 0;
static uint8_t s_saved_et0 = 0;
/* 临时诊断：记录执行 STOP 指令及返回的次数。 */
volatile uint16_t g_mcu_stop_enter_count = 0;
volatile uint16_t g_mcu_stop_return_count = 0;

void HAL_Power_Init(void)
{
    bit saved_ea;
    /*
     * 保存进入函数以前的总中断状态。
     * 不能简单：
     *      EA = 0; ... EA = 1;
     * 因为调用HAL之前EA可能本来就是0。
     * 正确做法：保存 → 关闭 → 配置 → 恢复
     */
    saved_ea = EA;

    EA = 0;
    /* 清空软件层累计状态。 */
    s_btm_ticks = 0;
    s_input_event = 0;
    INT2R =
        (INT2R & (uint8_t)(~POWER_INT2_INPUT_MASK)) |
        POWER_INT2_RISE_MASK;

    INT2F =
        (INT2F & (uint8_t)(~POWER_INT2_INPUT_MASK)) |
        POWER_INT2_FALL_MASK;
    /* ------------------------------------------------------------------------
     * BTM
     * --------------------------------------------------------------------- */
    /*
     * 第一步：
     * 关闭BTM中断。
     *
     * 避免我们正在修改BTM配置时发生中断。
     */
    IE1 &= (uint8_t)(~POWER_IE1_EBTM);

    BTMCON = POWER_BTM_PERIOD_125MS;

    IE1 |= POWER_IE1_EBTM |
           POWER_IE1_EINT2;

    BTMCON |= POWER_BTM_ENABLE;

    EA = saved_ea;
}

void HAL_Power_BTM_ISR(void)
{
    s_btm_ticks++;
}

void HAL_Power_Input_ISR(void)
{
    s_input_event = 1;
}

uint16_t HAL_Power_TakeTicks(void)
{
    uint16_t ticks;
    bit saved_ea;
    /*进入临界区*/
    saved_ea = EA;
    EA = 0;

    ticks = s_btm_ticks;
    s_btm_ticks = 0;
    EA = saved_ea;
    return ticks;
}

uint8_t HAL_Power_TakeInputEvent(void)
{
    uint8_t event;
    bit saved_ea;


    saved_ea = EA;

    EA = 0;


    event = s_input_event;

    s_input_event = 0;


    EA = saved_ea;


    return event;
}

uint8_t HAL_Power_InputPending(void)
{
    /*
     * uint8_t是8051原生可一次读取的宽度，
     * 不存在16位变量那种“读了一半被ISR修改”的问题。
     */
    return s_input_event;
}

void HAL_Power_Prepare(void)
{
    bit saved_ea;
    saved_ea = EA;
    EA = 0;
    s_saved_tr0 = TR0;

    s_saved_et0 = ET0;

    ET0 = 0;
    TR0 = 0;
    TF0 = 0;

    EA = saved_ea;
}

void HAL_Power_Resume(void)
{
    bit saved_ea;
    saved_ea = EA;
    EA = 0;

    TIM0_Mode1SetReloadCounter(
        POWER_TIM0_RELOAD);

    TF0 = 0;
    ET0 = s_saved_et0;
    TR0 = s_saved_tr0;

    EA = saved_ea;
}

void HAL_Power_StopOnce(void)
{
    if (!EA) {
        return;
    }

    if (s_input_event || s_btm_ticks) {
        return;
    }
		WDT_SetReload();
		g_mcu_stop_enter_count++;
		PCON |= 0x02;

    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
g_mcu_stop_return_count++;
}

/* 原始插入状态；正常业务的消抖仍由状态机完成。 */
uint8_t HAL_Power_TypeCIsPresent(void)
{
    return (P50 == 0) ? 1 : 0;
}
