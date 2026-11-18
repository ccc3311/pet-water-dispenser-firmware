#ifndef __HAL_POWER_H
#define __HAL_POWER_H
#include "SC_Init.h"
/*
 * 初始化低功耗唤醒源。
 *
 * BTM：每125ms产生一次中断，初始化后持续运行。
 *
 * INT2输入：
 * P06 / INT22：红外1，上升沿
 * P07 / INT23：主触摸，下降沿
 * P21 / INT25：红外3，上升沿
 *
 * P22没有外部中断，需在BTM唤醒后轮询。
 *
 * 调用位置：
 * 在SC_GPIO_Init、SC_INT_Init之后，
 * 进入主循环之前调用一次。
 */
void HAL_Power_Init(void);

/*
 * 获取并清空累计的BTM节拍。
 * 返回1代表已经发生一个125ms节拍，
 * 返回8代表已经发生八个节拍，名义时间为1秒。
 */
uint16_t HAL_Power_TakeTicks(void);

/*
 * 获取并清空外部输入事件标志。
 * 返回0：没有事件。
 * 返回1：发生过外部输入事件。
 */
uint8_t HAL_Power_TakeInputEvent(void);
/* 紧接 TakeInputEvent 后读取，获取本次取走事件的诊断快照。 */
uint8_t HAL_Power_GetInputDebug(void);

/*
 * 查看外部输入事件标志，不清空。
 * 用于入睡前最后检查。
 */
uint8_t HAL_Power_InputPending(void);

/*
 * 暂停Timer0及其中断。
 * 保存原来的TR0、ET0状态，
 * 供HAL_Power_Resume()恢复。
 * 不关闭总中断EA。
 * 不操作ADC、显示、PWM或P02。
 */
void HAL_Power_Prepare(void);

/*
 * 恢复Timer0正常调度。
 * 按当前工程的32MHz、1ms配置重新装载计数值。
 * 不恢复或开启水泵等负载。
 */
void HAL_Power_Resume(void);

/*
 * 执行一次STOP。
 *
 * 调用前提：
 * 1. HAL_Power_Init()已经执行；
 * 2. HAL_Power_Prepare()已经执行；
 * 3. EA保持开启；
 * 4. 应用层已完成休眠资格检查和外设准备。
 *
 * 如果存在未处理事件或BTM节拍，则直接返回，不睡。
 *
 * 正常中断唤醒后，此函数返回主循环。
 */
void HAL_Power_StopOnce(void);

/*
 * 以下两个接口仅供中断服务函数调用。
 */
/* 在BTMInterrupt()中调用 */
void HAL_Power_BTM_ISR(void);

/* 在INT0Interrupt()/INT2Interrupt()中调用 */
void HAL_Power_Input_ISR(void);

/* P5.0低电平表示Type-C已插入。 */
uint8_t HAL_Power_TypeCIsPresent(void);

#endif