#include "hal_tm1650.h"
#include <intrins.h> // 提供 _nop_() 函数

// --- 硬件引脚映射 ---
// 注意：在 SC_GPIO_Init 中，这两个脚需要配置为准双向口或开漏输出(上拉)
#define TM_SCL  P04
#define TM_SDA  P05

// --- TM1650 寄存器地址定义 ---
#define TM1650_CMD_SYS   0x48  // 系统控制命令 (显存操作)
#define TM1650_DIG1      0x68  // 显存地址1
#define TM1650_DIG2      0x6A  // 显存地址2
#define TM1650_DIG3      0x6C  // 显存地址3
#define TM1650_DIG4      0x6E  // 显存地址4

// --- 显存缓存区 ---
// 因为 TM1650 不支持单独修改某个位(只能整字节写入)，
// 我们需要在 RAM 里建一个显存缓存，修改缓存后再把整字节刷进去。
static uint8_t TM1650_Buffer[4] = {0, 0, 0, 0};

static void I2C_Delay(void) {
		// ！！！核心：加上 volatile 关键字 ！！！
    // 这会警告编译器：“绝对不许优化这段代码，必须老老实实执行”
    volatile uint8_t i = 15; 
//    while(i--);
}
static void I2C_Start(void) {
    TM_SDA = 1;
    TM_SCL = 1;
    I2C_Delay();
    TM_SDA = 0; // SCL高期间，SDA拉低产生起始信号
    I2C_Delay();
    TM_SCL = 0;
		I2C_Delay();
}

static void I2C_Stop(void) {
    TM_SCL = 0; 
    TM_SDA = 0; // 确保SDA在SCL拉高前是低电平
    I2C_Delay();
    TM_SCL = 1;
    I2C_Delay();
    TM_SDA = 1; // SCL高电平时，SDA拉高产生停止信号
    I2C_Delay();
}

static void I2C_WriteByte(uint8_t dat) {
    uint8_t i;
    for (i = 0; i < 8; i++) {
        TM_SCL = 0; // 先拉低时钟，允许数据变化
        I2C_Delay();
        
        if (dat & 0x80) {
            TM_SDA = 1;
        } else {
            TM_SDA = 0;
        }
        I2C_Delay(); // 等待数据稳定
        
        TM_SCL = 1; // 拉高时钟，让 TM1650 采样
        I2C_Delay(); 
        
        dat <<= 1;
    }
    
    // --- 极其关键的第 9 个时钟 (处理 ACK) ---
    TM_SCL = 0; // 拉低时钟，准备接收 ACK
    TM_SDA = 1; // ！！！MCU必须释放总线(写1)，允许 TM1650 拉低总线 ！！！
    I2C_Delay();
    
    TM_SCL = 1; // 产生第 9 个时钟，TM1650 此时应该拉低了 SDA
    I2C_Delay(); 
    
    TM_SCL = 0; // 结束第 9 个时钟
    I2C_Delay();
}

// 写入一个指令到 TM1650
static void TM1650_WriteCmd(uint8_t addr, uint8_t dat) {
    I2C_Start();
    I2C_WriteByte(addr);
    I2C_WriteByte(dat);
    I2C_Stop();
}


/**
 * @brief  模拟 I2C 读取一个字节
 * @return 读到的 8 bit 数据
 */
static uint8_t I2C_ReadByte(void) {
    uint8_t i;
    uint8_t dat = 0;

    // 重点：单片机准备读取数据前，必须释放 SDA 总线
    // (在准双向口/开漏模式下，写 1 代表释放总线，配置为输入)
    TM_SDA = 1; 
    I2C_Delay();

    for (i = 0; i < 8; i++) {
        dat <<= 1;
        
        TM_SCL = 1; // 时钟拉高，此时 TM1650 会把数据放到 SDA 上
        I2C_Delay();
        
        // 采样 SDA 数据
        if (TM_SDA) {
            dat |= 0x01;
        }
        
        TM_SCL = 0; // 时钟拉低，通知 TM1650 准备下一位
        I2C_Delay();
    }

    // 处理第 9 个时钟 (ACK 时钟位)
    // 根据 TM1650 数据手册图5，读数据时无需主机应答，只需给足时钟信号即可释放
    TM_SDA = 1; 
    I2C_Delay();
    TM_SCL = 1;
    I2C_Delay();
    TM_SCL = 0;

    return dat;
}

/**
 * @brief  读取 TM1650 当前的按键状态
 * @return 8位键值。如果没有按键按下，通常返回 0x00 (或其它非按下的释放码)
 */
uint8_t HAL_TM1650_ReadKey(void) {
    uint8_t keycode = 0;
    
    I2C_Start();
    I2C_WriteByte(0x4F);     // 发送读取按键的命令 0x4F
    keycode = I2C_ReadByte(); // 读取 1 个字节的键值
    I2C_Stop();
    
    return keycode;
}

// ==========================================
// 应用层 API 接口
// ==========================================

/**
 * @brief  初始化 TM1650
 */
void HAL_TM1650_Init(void) {
    // 0x31: 开启显示，3级亮度(0x31~0x37为不同亮度)，7段模式
    TM1650_WriteCmd(TM1650_CMD_SYS, 0x01); //原为0x31
    
//    HAL_LED_ClearAll(); // 上电清空显示
}

/**
 * @brief  清空所有指示灯
 */
void HAL_LED_ClearAll(void) {
    uint8_t i;
    for(i=0; i<4; i++) TM1650_Buffer[i] = 0;
    
    TM1650_WriteCmd(TM1650_DIG1, 0x00);
    TM1650_WriteCmd(TM1650_DIG2, 0x00);
    TM1650_WriteCmd(TM1650_DIG3, 0x00);
    TM1650_WriteCmd(TM1650_DIG4, 0x00);
}

/**
 * @brief  控制单颗 LED 的亮灭 (支持分级映射与逻辑反转)
 * @param  led_id: 灯光枚举 ID
 * @param  status: LED_ON 或 LED_OFF
 */
void HAL_LED_SetStatus(LedId_t led_id, uint8_t status) {
    uint8_t addr_index = 0;  // 对应 DIG1~DIG4 (0~3)
    uint8_t bit_mask = 0;    // 对应 SEG 的哪一位
    uint8_t is_inverted = 0; // 标记是否为“相反触发”
    uint8_t final_status = status; // 处理 "相反触发" 的防弹逻辑

    // ==========================================
    // 硬件走线严格映射区 (基于原理图 image_403ca0.png)
    // ==========================================
    switch (led_id) {
        // --- DIG2: 状态指示灯 (正向逻辑: 写1亮，写0灭) ---		
			//===============修改1：指示灯逻辑==================================
			  case LED_NO_WATER:        addr_index = 1; bit_mask = (1 << 1); break; // B1
        case LED_WATER_Q:         addr_index = 1; bit_mask = (1 << 2); break; // C1
        case LED_HEATING: 				addr_index = 1; bit_mask = (1 << 3); break; // D1
        case LED_MODE_CONT:       addr_index = 1; bit_mask = (1 << 4); break; // E1
        case LED_MODE_SENSE:      addr_index = 1; bit_mask = (1 << 5); break; // F1
        case LED_SW2:     			  addr_index = 1; bit_mask = (1 << 6); break; // G1
        case LED_SW1:   					addr_index = 1; bit_mask = (1 << 7); break; // DP1

        // --- DIG3: 感应指示灯组 (相反触发: 软件自动翻转) ---
        case LED_SENSE_IND_1: 		addr_index = 2; bit_mask = (1 << 2); break; // C1
        case LED_SENSE_IND_2: 		addr_index = 2; bit_mask = (1 << 3); break; // D1
        case LED_SENSE_IND_3: 		addr_index = 2; bit_mask = (1 << 4); break; // E1
        case LED_SENSE_IND_4: 		addr_index = 2; bit_mask = (1 << 5); break; // F1
        case LED_SENSE_IND_5: 		addr_index = 2; bit_mask = (1 << 6); break; // G1

        default: return; // 非法 ID 保护
    }

    // ==========================================
    // 修改显存缓存 (Read-Modify-Write)
    // ==========================================
    if (final_status == LED_ON) {
        TM1650_Buffer[addr_index] |= bit_mask;  // 置 1
    } else {
        TM1650_Buffer[addr_index] &= ~bit_mask; // 清 0
    }
    
    // ==========================================
    // 局部刷新：将更新后的单字节推送到芯片，拒绝全局刷屏
    // ==========================================
    switch (addr_index) {
        case 0: TM1650_WriteCmd(TM1650_DIG1, TM1650_Buffer[0]); break;
        case 1: TM1650_WriteCmd(TM1650_DIG2, TM1650_Buffer[1]); break;
        case 2: TM1650_WriteCmd(TM1650_DIG3, TM1650_Buffer[2]); break;
        case 3: TM1650_WriteCmd(TM1650_DIG4, TM1650_Buffer[3]); break;
    }
}


/**
 * @brief  将所有 DIG 的 DP 设置为高电平
 * @note   注意：物理引脚输出的仍是带有消隐间隔的脉冲波，并非纯直流高电平
 */
void HAL_LED_SetAllSegaHigh1(void) {
    uint8_t i;
//    uint8_t sega_mask = 0x80; // DP 
        uint8_t sega_mask = 0xff; // ALL 

    for(i = 0; i < 4; i++) {
        TM1650_Buffer[i] |= sega_mask; // 置 1
    }
    
    // 2. 将修改后的 4 个字节全部刷新到 TM1650
    TM1650_WriteCmd(TM1650_DIG1, TM1650_Buffer[0]);
    TM1650_WriteCmd(TM1650_DIG2, TM1650_Buffer[1]);
    TM1650_WriteCmd(TM1650_DIG3, TM1650_Buffer[2]);
    TM1650_WriteCmd(TM1650_DIG4, TM1650_Buffer[3]);
}



// ==========================================
// 应用层 API 接口 (新增电源控制)
// ==========================================

/**
 * @brief  软件关闭 TM1650 显示 (进入低功耗模式)
 * @note   在 P02 物理断电前调用，确保芯片安全下电
 */
void HAL_TM1650_PowerOff(void) {
    TM1650_WriteCmd(TM1650_CMD_SYS, 0x00); // 0x00: 关闭显示
}

/**
 * @brief  软件开启 TM1650 显示
 * @note   通常由 HAL_TM1650_Init() 代替执行
 */
void HAL_TM1650_PowerOn(void) {
    TM1650_WriteCmd(TM1650_CMD_SYS, 0x01); // 0x01: 开启显示
}