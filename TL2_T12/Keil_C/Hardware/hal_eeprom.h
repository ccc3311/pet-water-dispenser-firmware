#ifndef __HAL_EEPROM_H
#define __HAL_EEPROM_H

#include "tuya_type.h"
#include "sc92f_iap.h"

// 统一存放在 0x0000 基地址，总共占用 12 个字节
#define EEPROM_DATA_ADDR        0x0000  
#define EEPROM_TIME_LIMIT       0xF0   // 与官方例程一致，窗口留足   

// 【统一接口】保存和读取所有记忆数据
void HAL_EEPROM_Save_AllData(void);
void HAL_EEPROM_Load_AllData(void);

#endif /* __HAL_EEPROM_H */