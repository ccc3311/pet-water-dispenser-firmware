#include "sc92f_lpd.h"
#include "hal_eeprom.h"
#include "hal_lpd.h"

/**
 * @brief LPD掉电检测初始化
 */
void HAL_LPD_Init(void)
{
    LPD_DeInit(); // 复位寄存器

    // 1. 设置门限阈值：当 VDD 跌破 2.85V 时触发
    // 注意：如果你的系统是电池直供且电压会低于2.85V，这里必须调低！
    LPD_VtripConfig(LPD_VTRIP_2_85V);

    // 2. 开启 LPD 中断，并设置为高优先级 (抢占所有普通业务)
    LPD_ITConfig(ENABLE, HIGH);

    // 3. 使能 LPD 模块
    LPD_Cmd(ENABLE);
}
