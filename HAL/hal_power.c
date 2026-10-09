#include "hal_power.h"
#include <intrins.h>     /* Keil C51：_nop_() */

/*
 * 当前Timer0配置：
 * 系统时钟：32MHz
 * Timer0时钟：FSYS / 1
 * 计数方式：16位模式1
 * 中断周期：1ms
 *
 * 重装值 = 65536 - 32000 = 33536
 *
 * 后续若修改系统时钟或Timer0分频，
 * 这里必须与SC_TIM0_Init()同步修改。
 */
#define POWER_TIM0_RELOAD       33536U

/*
 * IE1中断使能位：
 * bit2：EBTM
 * bit3：EINT2
 */
#define POWER_IE1_EBTM          0x04
#define POWER_IE1_EINT2         0x08

/*
 * BTMCON：
 * bit7：ENBTM，启动BTM
 * bit6：BTMIF，中断标志
 * bit3~0：周期选择
 * 0011：125ms
 */
#define POWER_BTM_ENABLE        0x80
#define POWER_BTM_PERIOD_125MS  0x03

///*
// * INT2输入选择：
// *
// * bit2：INT22，对应P06
// * bit3：INT23，对应P07
// * bit5：INT25，对应P21
// */
//#define POWER_INT2_INPUT_MASK   0x2C
///* P06、P21：高有效，使用上升沿 */
//#define POWER_INT2_RISE_MASK    0x24
///* P07：低有效，使用下降沿 */
//#define POWER_INT2_FALL_MASK    0x08

/*
 * 本模块管理 INT22～INT25：
 * P0.6：红外输入，上升沿
 * P0.7：主触摸输入，下降沿
 * P2.0：Wi-Fi唤醒输入，上升沿
 * P2.1：Wi-Fi控制输出，关闭其外部中断边沿
 */
#define POWER_INT2_INPUT_MASK   0x3Cu
#define POWER_INT2_RISE_MASK    0x14u
#define POWER_INT2_FALL_MASK    0x08u

/*
 * 中断负责增加，主循环负责读取并清空。
 * 8051是8位CPU，读取16位变量不是一次完成，
 * 因此主循环消费它时需要短暂关闭总中断。
 */
static volatile uint16_t s_btm_ticks = 0;


/*
 * 外部输入事件锁存。
 * 即使输入脉冲已经结束，标志仍保留为1，
 * 直到主循环主动消费。
 */
static volatile uint8_t s_input_event = 0;


/*
 * 保存Timer0原来的运行和中断使能状态。
 *
 * 这些变量只由主循环使用，不需要volatile。
 */
static uint8_t s_saved_tr0 = 0;
static uint8_t s_saved_et0 = 0;

/* P5.0：低电平表示 Type-C 已插入。 */
uint8_t HAL_Power_TypeCIsPresent(void)
{
    return (P50 == 0) ? 1 : 0;
}

void HAL_Power_Init(void)
{
    bit saved_ea;

    /*
     * 配置多个相关寄存器时，暂时关闭中断。
     * 完成后恢复调用前的EA状态。
     */
    saved_ea = EA;
    EA = 0;

    s_btm_ticks = 0;
    s_input_event = 0;

    /*
     * 配置INT2三路输入的边沿。
     *
     * 先清除这三路的旧配置，再设置新配置。
     * 保留INT20、INT21、INT24的原有配置。
     *
     * INT2R：上升沿使能寄存器
     * INT2F：下降沿使能寄存器
     */
    INT2R =
        (INT2R & (uint8_t)(~POWER_INT2_INPUT_MASK)) |
        POWER_INT2_RISE_MASK;

    INT2F =
        (INT2F & (uint8_t)(~POWER_INT2_INPUT_MASK)) |
        POWER_INT2_FALL_MASK;

    /*
     * BTM配置顺序：
     *
     * 1. 关闭BTM中断；
     * 2. 停止BTM并设置125ms周期；
     * 3. 开启BTM和INT2中断；
     * 4. 启动BTM。
     */

    IE1 &= (uint8_t)(~POWER_IE1_EBTM);

    /*
     * ENBTM=0：停止
     * BTMIF=0：清除旧标志
     * BTMFS=0011：125ms
     */
    BTMCON = POWER_BTM_PERIOD_125MS;

    IE1 |= POWER_IE1_EBTM | POWER_IE1_EINT2;

    BTMCON |= POWER_BTM_ENABLE;

    /*
     * 不在每次STOP前重启BTM。
     *
     * BTM持续运行，才能让低频节拍包含
     * MCU短暂醒着巡检的时间。
     */
    EA = saved_ea;
}


/*
 * ==========================================================
 * 中断侧接口
 * ==========================================================
 */

void HAL_Power_BTM_ISR(void)
{
    /*
     * SC92L7533手册规定：
     * CPU响应BTM中断后，BTMIF由硬件自动清除。
     *
     * 这里只记录节拍，不执行ADC、UI或状态机。
     */
    s_btm_ticks++;
}


void HAL_Power_Input_ISR(void)
{
    /*
     * 锁存事件。
     * 此处不直接置g_Device.ir_triggered，
     * 因为同一个INT2入口还包含主触摸。
     *
     * 醒后交给正常扫描完成来源判断和消抖。
     */
    s_input_event = 1;
}


/*
 * ==========================================================
 * 主循环消费接口
 * ==========================================================
 */

uint16_t HAL_Power_TakeTicks(void)
{
    uint16_t ticks;
    bit saved_ea;

    saved_ea = EA;
    EA = 0;

    /*
     * 读取与清零放在同一小段临界区。
     * 避免中断在读取后、清零前增加的新节拍被覆盖。
     */
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
     * 仅查看，不消费。
     * 8位标志不需要16位变量那样的组合读取。
     */
    return s_input_event;
}


/*
 * ==========================================================
 * 暂停正常调度
 * ==========================================================
 */

void HAL_Power_Prepare(void)
{
    bit saved_ea;

    saved_ea = EA;
    EA = 0;

    /*
     * 保存状态。
     * 因此Prepare不能在尚未Resume时再次调用，
     * 否则会用“已停止”的状态覆盖原来的状态。
     */
    s_saved_tr0 = TR0;
    s_saved_et0 = ET0;

    /*
     * 顺序：
     * 关闭Timer0中断 → 停止计数 → 清除旧溢出标志。
     */
    ET0 = 0;
    TR0 = 0;
    TF0 = 0;

    /*
     * EA恢复后，BTM及外部中断仍能运行。
     *
     * Timer0停止后：
     * - 不再产生正常毫秒时间片；
     * - 触摸扫描暂停；
     * - 蜂鸣器毫秒状态机暂停。
     *
     * 所以应用层必须先确认蜂鸣器空闲、
     * 触摸操作已经结算。
     */
    EA = saved_ea;
}


/*
 * ==========================================================
 * 恢复正常调度
 * ==========================================================
 */

void HAL_Power_Resume(void)
{
    bit saved_ea;

    saved_ea = EA;
    EA = 0;

    /*
     * 从新的1ms周期开始计时。
     * 不沿用睡前剩余的半个Timer0周期。
     */
    TIM0_Mode1SetReloadCounter(POWER_TIM0_RELOAD);

    TF0 = 0;

    /*
     * 恢复原来的Timer0使能状态。
     * 当前工程正常情况下：
     * s_saved_et0=1，s_saved_tr0=1。
     */
    ET0 = s_saved_et0;
    TR0 = s_saved_tr0;

    EA = saved_ea;
}


/*
 * ==========================================================
 * 执行一次STOP
 * ==========================================================
 */

/* 执行一次STOP；返回后由应用层决定巡检或恢复正常调度。 */
void HAL_Power_StopOnce(void)
{
    if (!EA) {
        return;
    }

    if (s_input_event || s_btm_ticks) {
        return;
    }

    WDT_SetReload();
    PCON |= 0x02;

    /* 保留STOP之后紧接的8条NOP。 */
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    _nop_();
}
