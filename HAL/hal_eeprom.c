#include "hal_eeprom.h"
#include "HeadFiles/SysFunVarDefine.h" 
#include "app_fsm.h"   

extern unsigned char IapWriteFlag; 

/**
 * @brief 将【清洗+滤芯】数据打包保存到单片机 EEPROM
 */
void HAL_EEPROM_Save_AllData(void)
{
    uint8_t write_buf[16]; // 精简为 16 字节
    
    // --- 打包清洗数据 (前 6 字节) ---
    write_buf[0] = (uint8_t)(g_Device.target_clean_days >> 8);
    write_buf[1] = (uint8_t)(g_Device.target_clean_days & 0xFF);
    write_buf[2] = (uint8_t)(g_Device.last_clean_timestamp >> 24);
    write_buf[3] = (uint8_t)(g_Device.last_clean_timestamp >> 16);
    write_buf[4] = (uint8_t)(g_Device.last_clean_timestamp >> 8);
    write_buf[5] = (uint8_t)(g_Device.last_clean_timestamp & 0xFF);
    
    // --- 打包滤芯数据 (中间 6 字节) ---
    write_buf[6] = (uint8_t)(g_Device.target_filter_days >> 8);
    write_buf[7] = (uint8_t)(g_Device.target_filter_days & 0xFF);
    write_buf[8] = (uint8_t)(g_Device.last_filter_timestamp >> 24);
    write_buf[9] = (uint8_t)(g_Device.last_filter_timestamp >> 16);
    write_buf[10]= (uint8_t)(g_Device.last_filter_timestamp >> 8);
    write_buf[11]= (uint8_t)(g_Device.last_filter_timestamp & 0xFF);

		/* A6 表示包含报警标志的新格式。 */
		write_buf[12] = 0x5A;
		write_buf[13] = 0xA6;
		write_buf[14] = g_Device.req_clean_alarm ? 1 : 0;
		write_buf[15] = g_Device.req_filter_alarm ? 1 : 0;

    EA = 0; // 关闭全局中断

    IapWriteFlag = 0x69; 
    IAP_SectorErase(IAP_MEMTYPE_EEPROM, EEPROM_DATA_ADDR, EEPROM_TIME_LIMIT);
    
    IapWriteFlag = 0x69;
    IAP_ProgramByteArray(EEPROM_DATA_ADDR, write_buf, 16, IAP_MEMTYPE_EEPROM, EEPROM_TIME_LIMIT);
    
    IapWriteFlag = 0x00; 
    
    EA = 1; // 恢复全局中断
}

/**
 * @brief 开机统一读取【清洗+滤芯】数据
 */
void HAL_EEPROM_Load_AllData(void)
{
    uint8_t read_buf[16]; 
    unsigned char need_save = 0; 

    IAP_ReadByteArray(EEPROM_DATA_ADDR, read_buf, 16, IAP_MEMTYPE_EEPROM);

    // 校验魔术字
		/* A5 是旧格式，A6 是新增报警标志的格式。 */
		if (read_buf[12] != 0x5A ||
				(read_buf[13] != 0xA5 && read_buf[13] != 0xA6)) {
				App_Restore_Factory_Settings();
				return;
		}
    
    // --- 解析清洗数据 ---
    g_Device.target_clean_days = ((unsigned int)read_buf[0] << 8) | read_buf[1];
    g_Device.last_clean_timestamp = ((unsigned long)read_buf[2] << 24) | 
                                    ((unsigned long)read_buf[3] << 16) | 
                                    ((unsigned long)read_buf[4] << 8)  | 
                                    read_buf[5];
                                    
    // --- 解析滤芯数据 ---
    g_Device.target_filter_days = ((unsigned int)read_buf[6] << 8) | read_buf[7];
    g_Device.last_filter_timestamp = ((unsigned long)read_buf[8] << 24) | 
                                     ((unsigned long)read_buf[9] << 16) | 
                                     ((unsigned long)read_buf[10] << 8) | 
                                     read_buf[11];

		/* 0 天是合法状态；大于 365 也包含未初始化的 0xFFFF。 */
		if (g_Device.target_clean_days > 365U) {
				g_Device.target_clean_days = DEFAULT_CLEAN_DAYS;
				g_Device.last_clean_timestamp = 0;
				need_save = 1;
		}

		if (g_Device.target_filter_days > 365U) {
				g_Device.target_filter_days = DEFAULT_FILTER_DAYS;
				g_Device.last_filter_timestamp = 0;
				need_save = 1;
		}
    
		/* 旧记录没有报警信息；新记录恢复保存的到期状态。 */
		g_Device.req_clean_alarm = 0;
		g_Device.req_filter_alarm = 0;

		if (read_buf[13] == 0xA6) {
				if (g_Device.target_clean_days == 0 &&
						read_buf[14] == 1) {
						g_Device.req_clean_alarm = 1;
				}

				if (g_Device.target_filter_days == 0 &&
						read_buf[15] == 1) {
						g_Device.req_filter_alarm = 1;
				}
		}

    if (need_save) {
        HAL_EEPROM_Save_AllData();
    }
}