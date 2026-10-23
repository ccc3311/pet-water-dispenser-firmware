#include "hal_adc.h"
#include "app_fsm.h"     // 借用 WATER_GOOD 等枚举
#include "hal_buzzer.h"
//#include <stdio.h>    // ！！！必须包含 stdio.h 才能用 printf ！！！

// ==========================================
// 调试开关：无视缺水保护 (投产时务必改为 0 ！)
// ==========================================
#define DEBUG_IGNORE_WATER_EMPTY  1  // 1: 开启缺水调试(无视缺水), 0: 关闭

// ==========================================
// 硬件参数配置区 (需根据实际 PCB 调整)
// ==========================================
// SC92F8462B 的 ADC 是 12位 (0~4095)
#define ADC_RESOLUTION      4095

// 假设 ADC 参考电压设定为内部 VDD (比如 5.0V) -> 5000mV
#define VREF_MV             2400 
//#define VREF_MV             5000 

// 电池/适配器分压电路的比例 (非常重要！)
// 真实电压 = ADC引脚电压 * (R1+R2)/R2 = ADC引脚电压 * 6
// TL2为300k和150k分压增益为3
#define VOLTAGE_DIV_RATIO   3 

#define BAT_ADC_CHANNEL 0u

//adc超时
static uint8_t s_adc_read_failed = 0;

// 用于缓存算出来的真实 PPM 数值
static uint16_t cached_tds_ppm = 0;
static uint32_t numerator;
static uint32_t denominator;
// ！！！新增：缺水检测专属 GPIO 引脚定义 ！！！
#define WATER_EMPTY_PIN     P01  // 在调试完其他功能后再引入缺水功能

// TDS 缺水与水质判定阈值 (需要等板子回来后实测微调)
#define TDS_GOOD_THRESHOLD  150  // ADC < 1500 认为是纯净水（此处判断为算法值）

// ==========================================
// 内部静态变量 (用于数据缓存和滤波计数)
// ==========================================
 uint16_t cached_sys_voltage_mv = 0;
static uint8_t  cached_water_quality = WATER_GOOD;
// 新增：温度缓存变量 (默认 25℃)
 int8_t   cached_temperature_c = 25;
// ==========================================
// 新版电池电量三档定义 (映射为百分比兼容云端)(对应涂鸦枚举 High=0, Middle=1, Low=2)：
// ==========================================
#define BAT_TIER_HIGH  0  // 高电量 (>3.9V)
#define BAT_TIER_MID   1   // 中电量 (3.5V ~ 3.9V)
#define BAT_TIER_LOW   2   // 低电量 (<3.5V)

// ==========================================
// 负载与充电电压补偿参数
// ==========================================
#define PUMP_SAG_MV_OFFSET     1   // 水泵抽水时的内阻压降补偿
#define CHARGE_RISE_MV_OFFSET  1  // 充电时 1A 电流带来的内阻压升补偿 (根据日志得出)
// 水泵满载时导致电池电压瞬间被拉低的补偿值 (单位: mV)
// 你可以在水泵启动时，观察串口打印的电压下降了多少，填到这里
// 用于缓存算出来的真实电池百分比 (0-100)
static uint8_t cached_bat_soc = 100; 

// ==========================================
// ========================================================
// [升级] 连续进行 10 次独立的防极化采样
// ========================================================
#define TDS_SAMPLE_NUM 6
uint16_t tds_samples[TDS_SAMPLE_NUM];

// ==========================================
// [51优化版] NTC 温度计算逻辑 (支持 BAT 电压波动)
// 纯 uint32_t 运算，绝无 uint64，适配 100K NTC + 510K 上拉
// ==========================================

// --- 硬件参数配置 ---
#define NTC_PULLUP_RESISTOR_OHM     510000UL  // R13 上拉电阻: 510K
#define NTC_NOMINAL_RESISTANCE_OHM  100000UL  // NTC 标称阻值: 100K (25℃时)

// --- 100K B=3950 NTC 标准阻值表 ---
// 格式: { 温度(℃), 阻值(欧姆) }
// 使用 uint32_t 存储，最大 32 万，不会溢出
typedef struct {
    int8_t temp;
    uint32_t resistance;
} NTC_Res_Table_t;

static const NTC_Res_Table_t NTC_RES_LUT[] = {
    {0,   326500UL}, 
    {10,  200800UL}, 
    {20,  126300UL}, 
    {25,  100000UL}, // 25度基准点
    {30,  79900UL}, 
    {40,  51300UL}, 
    {50,  33600UL}, 
    {60,  22500UL}, 
    {70,  15400UL}, 
    {80,  10800UL}, 
    {90,  7700UL}, 
    {100, 5600UL}
};

#define NTC_RES_TABLE_SIZE (sizeof(NTC_RES_LUT) / sizeof(NTC_RES_LUT[0]))

/**
 * @brief  [51优化版] 通过实时 BAT 电压和 ADC 值计算 100K NTC 温度
 * @param  adc_val: ADC 原始读数 (0-4095)
 * @retval 温度值 (℃)
 */
static int8_t Convert_NTC_ADC_To_Temp(uint16_t adc_val) {
    uint32_t v_adc_mv;
    uint32_t v_bat_mv;
    uint32_t r_ntc;
    uint8_t i;
    int8_t temp_calc;
		uint32_t numerator;
		uint32_t denominator;
    // 1. 获取实时 BAT 电压 (全局变量，单位 mV)
		v_bat_mv = 3300;//现在方案温度改为固定3.3V
    // 2. 计算 ADC 引脚上的实际电压 (mV)
    // 公式: V_adc = ADC_val * VREF / 4095
    // 注意：adc_val * 2400 最大约 982 万，uint32_t 完全够用
    v_adc_mv = ((uint32_t)adc_val * VREF_MV) / ADC_RESOLUTION;

    // 3. 硬件容错 1：ADC 读数饱和 (>= 4090)
    // 说明 NTC 阻值极大 (断路或极冷)，或者输入电压超过了 VREF(2400mV)
    if (adc_val >= 4090) {
        return 100; // 故意返回假高温，触发 FSM 停止加热防灾 (保持你原代码逻辑)
    }
    
    // 4. 硬件容错 2：防止分母为 0 或负数
    // 如果 V_adc >= V_bat，说明物理电路异常或 ADC 读数错乱
    if (v_adc_mv >= v_bat_mv) {
        return 100; // 异常高温保护
    }

    // 5. 计算 NTC 实时阻值 (核心公式)
    // 公式: R_ntc = (R_pullup * V_adc) / (V_bat - V_adc)
    // 分子最大: 510,000 * 2400 = 1,224,000,000 (12.24亿)
    // uint32_t 最大值: 4,294,967,295 (42.9亿) -> 绝对不会溢出！
     numerator = NTC_PULLUP_RESISTOR_OHM * v_adc_mv;
     denominator = v_bat_mv - v_adc_mv;
    
    r_ntc = numerator / denominator;

    // 6. 查阻值表 -> 温度 (线性插值)
    // 表中阻值是降序排列的 (326K -> 5.6K)
    
    // 边界检查：阻值过大 (太冷，超出表格下限)
    if (r_ntc >= NTC_RES_LUT[0].resistance) {
        return NTC_RES_LUT[0].temp; // 返回 0℃
    }
    
    // 边界检查：阻值过小 (太热，超出表格上限)
    if (r_ntc <= NTC_RES_LUT[NTC_RES_TABLE_SIZE - 1].resistance) {
        return NTC_RES_LUT[NTC_RES_TABLE_SIZE - 1].temp; // 返回 100℃
    }

    // 遍历寻找区间
    for (i = 0; i < NTC_RES_TABLE_SIZE - 1; i++) {
        if (r_ntc <= NTC_RES_LUT[i].resistance && r_ntc > NTC_RES_LUT[i+1].resistance) {
            
            uint32_t r_diff = NTC_RES_LUT[i].resistance - NTC_RES_LUT[i+1].resistance;
            uint32_t r_offset = NTC_RES_LUT[i].resistance - r_ntc;
            int8_t temp_step = NTC_RES_LUT[i+1].temp - NTC_RES_LUT[i].temp; // 通常是 10 或 5
            
            // 线性插值公式: 当前温度 = 起点温度 + (偏移比例 * 温度步长)
            // r_offset * temp_step 最大约 12万 * 10 = 120万，uint32_t 无压力
            temp_calc = NTC_RES_LUT[i].temp + ((r_offset * temp_step) / r_diff);
            return temp_calc;
        }
    }

    return 25; // 兜底容错返回
}

// ==========================================
// TDS 物理常量与标定参数配置
// ==========================================
#define R_FIXED              1500UL   // 硬件分压电阻阻值 (Ω)
#define CELL_CONSTANT         2175000UL  // 电导池常数 (放大 10^6 倍)//1179381UL   513385UL

#define TDS_COEFFICIENT      55UL     // TDS 转换系数 (0.55 的 100 倍)

/**
 * @brief  [纯整数物理模型版] TDS 转换算法
 * @param  adc_val: 滤波后的正反交替差分 ADC 值 (0~4095)
 * @param  temp_c:  实时水温 (摄氏度)
 * @retval 补偿到 25℃ 时的真实 TDS PPM 值 (0~999)
 */
static uint16_t Convert_ADC_To_TDS_ppm(uint16_t adc_val, int8_t temp_c) {
    uint32_t r_probe;       // 水体物理电阻 (Ω)
    uint32_t conductivity;  // 未补偿电导率 (μS/cm)
    uint32_t ec25;          // 25℃ 等效标准电导率 (μS/cm)
    uint16_t tds_ppm;
    int16_t  temp_coef;     // 温度补偿系数 (放大 100 倍)

    // 1. 探空与防呆保护 (离水或极纯水)
    if (adc_val >= 4000) {  
        return 0;
    }
    if (adc_val == 0) {     
        adc_val = 1;
    }

		// 2. 计算溶液物理电阻 R_probe (单位: Ω)
    // ！！！核心修复：翻转了分子与分母，匹配探针接地端的硬件模型 ！！！
    // 公式: R_probe = (R_FIXED * ADC) / (4095 - ADC)
    r_probe = (R_FIXED * (uint32_t)adc_val) / (4095UL - (uint32_t)adc_val);
    if (r_probe == 0) r_probe = 1;

    // 3. 计算实时物理电导率 (单位: μS/cm)
    // 公式: conductivity = CELL_CONSTANT / R_probe
    conductivity = CELL_CONSTANT / r_probe;

    // 4. 温度补偿 (统一折算到 25℃)
    // 补偿公式系数: 100 + 2 * (T - 25)
    temp_coef = 100 + 2 * ((int16_t)temp_c - 25);
    if (temp_coef < 10) temp_coef = 10; // 防极寒温度下分母过小

    // 计算补偿后的电导率
    // 溢出验证: conductivity 通常在 1000 以内，乘 100 为十万级，uint32_t 绝对安全
    ec25 = (conductivity * 100UL) / (uint32_t)temp_coef;

    // 5. 转换为最终的 TDS PPM 值
    // 公式: TDS = (EC25 * 55) / 100
    // 溢出验证: EC25 * 55 同样在百万级别，不会溢出
    tds_ppm = (uint16_t)((ec25 * TDS_COEFFICIENT) / 100UL);	
		// ========================================================
    // [新增] 基于实测数据的线性拟合校准 (y = 0.93x - 8)
    // 修正因探针间距误差、PCB走线寄生电容带来的系统性偏移
    // ========================================================
    {
        uint32_t calibrated_tds = ((uint32_t)tds_ppm * 93) / 100; // 乘以 0.93 的斜率
        if (calibrated_tds > 8) {
            tds_ppm = (uint16_t)(calibrated_tds - 8); // 减去 8 的截距偏移
        } else {
            tds_ppm = 0; // 防跌落为负数
        }
    }
    // 6. UI 显示限幅保护
    if (tds_ppm > 999) tds_ppm = 999;

    return tds_ppm;
}

/**
 * @brief  32MHz 1T 单片机专属的微秒延时 (基于 Keil O8 优化等级)
 * @param  us: 需要延时的微秒数
 */
static void Delay_us(uint16_t us) {
    uint8_t i;
    while (us--) {
        // while(us--) 判断本身约消耗 8~10 个时钟周期
        // 还需要补充 22 个系统时钟 (32MHz 下 1us = 32 个时钟)
        i = 7; 
        while (--i); // 每次 --i 约消耗 3 个时钟，7 * 3 = 21 个时钟
    }
}

// ==========================================
// 负载与充电电压补偿参数
// ==========================================
/**
 * @brief  [新版三档算法] 计算并过滤电池电量档位
 * @param  voltage_mv:   ADC读取到的系统电压
 * @param  is_charging:  当前是否插着电源 (POWER_AC)
 * @param  pump_is_on:   当前水泵是否正在运行
 * @retval 映射后的电量百分比 (100:高, 50:中, 10:低)
 */
    uint8_t raw_tier;
    static uint8_t current_tier = 0xFF; // 当前正式档位 (0xFF代表开机未初始化)
    static uint8_t debounce_cnt = 0;    // 防抖计数器
static uint8_t Calculate_Battery_Tier(uint16_t voltage_mv, uint8_t is_charging, uint8_t pump_is_on) {
    // 1. 动态硬件补偿 (维持原有优秀逻辑)
    if (!is_charging && pump_is_on) {
        voltage_mv += PUMP_SAG_MV_OFFSET;
    } else if (is_charging) {
        if (voltage_mv > CHARGE_RISE_MV_OFFSET) {
            voltage_mv -= CHARGE_RISE_MV_OFFSET; 
        }
    }

    // 2. 【核心修复】：引入区间迟滞 (Hysteresis)，彻底消除跳档 Bug
			if (current_tier == 0xFF) {
					if (voltage_mv >= 3800U) {
							raw_tier = BAT_TIER_HIGH; /* 高电量起点改为3.8V。 */
					} else if (voltage_mv >= 3600U) {
							raw_tier = BAT_TIER_MID;
					} else {
							raw_tier = BAT_TIER_LOW;  /* 3.6V以下进入提醒档，不直接决定停泵。 */
					}
			} else {
					raw_tier = current_tier;

					if (current_tier == BAT_TIER_HIGH) {
							if (voltage_mv < 3800U) {
									raw_tier = BAT_TIER_MID;
							}
					}
					else if (current_tier == BAT_TIER_MID) {
							if (voltage_mv >= 3850U) {
									raw_tier = BAT_TIER_HIGH; /* 回升保留50mV迟滞。 */
							} else if (voltage_mv < 3600U) {
									raw_tier = BAT_TIER_LOW;
							}
					}
					else if (current_tier == BAT_TIER_LOW) {
							if (voltage_mv >= 3650U) {
									raw_tier = BAT_TIER_MID;  /* 回升保留50mV迟滞。 */
							}
					}
			}

    // 3. 状态机防抖过滤
    if (current_tier == 0xFF) {
        current_tier = raw_tier;
        return current_tier;
    }

    // 只有连续 3 秒 (6 * 500ms) 稳定在新的 raw_tier 才确认切换
    if (raw_tier != current_tier) {
        debounce_cnt++;
        if (debounce_cnt >= 6) {
            current_tier = raw_tier;
            debounce_cnt = 0;
            // HAL_Buzzer_Beep_3s(); // 如果需要档位切换鸣叫，解除注释
        }
    } else {
        debounce_cnt = 0; 
    }

    return current_tier;
}
/**
 * @brief  底层单次 ADC 读取函数 (阻塞式)
 * @param  channel: 对应 AINx 的通道号 (例如 AIN4 传 4)
 * @retval 12 位 ADC 转换结果 (0~4095)
 */
static uint16_t ADC_Read_Channel(uint8_t channel) {
    uint16_t adc_result = 0;
		uint16_t timeout_cnt = 500; // 防卡死超时计数器 (根据主频微调，确保大于硬件转换时间)    
	
    // 1. 暴力切通道，绝不使用 |= ！
    // 0x80 就是二进制 1000 0000，确保 ADCEN=1, ADCSTA=0, EOC=0
    ADCCON = 0x80 | (channel & 0x0F);
    
		// 2. ！！！核心修复：给高阻抗电路充足的采样保持时间 ！！！
    // 100k 的外部电阻，RC 常数较大，我们给它足足 50 微秒的充电时间！
    // 这样能彻底阻断上一个通道留下的“残影电荷”
    Delay_us(20);
    
    // 3. 暴力启动转换！
    // 0xC0 就是二进制 1100 0000，确保 ADCEN=1, ADCSTA=1 (启动), EOC=0
    ADCCON = 0xC0 | (channel & 0x0F);
    
		// 4. 等待硬件回应 (加入防跑飞超时)
    while (!(ADCCON & 0x20)) {
        if (--timeout_cnt == 0) {
            // 超时退出！说明硬件 ADC 模块可能受干扰死锁
            ADCCON = 0x80 | (channel & 0x0F); // 强行复位 ADC 状态
						s_adc_read_failed = 1;
						return 0;
        }
    }
    
    // 5. 硬件已置位 EOC，数据已准备好，立即读取！
//    adc_result = ((uint16_t)(ADCVH & 0x0F) << 8) | ADCVL;
		adc_result = ((uint16_t)ADCVH << 4) | (ADCVL >> 4);    
    // 6. 暴力清零 EOC，恢复到待命状态
    ADCCON = 0x80 | (channel & 0x0F);
    
    return adc_result;
}

/**
 * @brief  集中采集所有传感器数据 (每 500ms 调用一次)
 */
void HAL_ADC_Sample_All(void) {
    uint16_t adc_val;
    uint32_t temp_mv;
    uint8_t i, j;
    uint16_t temp_swap;
    uint32_t tds_sum = 0;
 					uint16_t adc_fwd; // 读取正向ADC
					uint16_t adc_rev; // 读取反向ADC
	
		s_adc_read_failed = 0;
    // ---------------------------------------------------------
    // 任务 1：系统电压采集 (Pin 22 / AIN0) - 带 3 次平均滤波
    // ---------------------------------------------------------
		adc_val  = ADC_Read_Channel(BAT_ADC_CHANNEL);
		adc_val += ADC_Read_Channel(BAT_ADC_CHANNEL);
		adc_val += ADC_Read_Channel(BAT_ADC_CHANNEL);
		adc_val /= 3;

    // 电压换算公式：mV = (adc_val * VREF_MV / 4095) * 分压比
    temp_mv = ((uint32_t)adc_val * VREF_MV) / ADC_RESOLUTION;
    cached_sys_voltage_mv = (uint16_t)(temp_mv * VOLTAGE_DIV_RATIO);
		// ========================================================
    // [新增] 刷新电池电量百分比
    // ========================================================
    {
        uint8_t is_ac = (g_Device.power_mode == POWER_AC);
        // 判断当前水泵是否正在耗电（如果你还有 UVC 也可以算进去，看功率占比）
        uint8_t pump_is_on = (g_Device.state == STATE_SENSING || g_Device.state == STATE_CONTINUOUS);
        
        // 调用算法更新缓存
        cached_bat_soc = Calculate_Battery_Tier(cached_sys_voltage_mv, is_ac, pump_is_on);
    }

    // ---------------------------------------------------------
    // 任务 2：NTC 温度采集 (Pin 27 / AIN5)
    // ---------------------------------------------------------
    adc_val  = ADC_Read_Channel(5);
    adc_val += ADC_Read_Channel(5);
    adc_val += ADC_Read_Channel(5);
    adc_val /= 3;
//                    printf("adcadc  : %u \r\n", ADC_Read_Channel(3));

    // 调用查表算法，刷新温度缓存
    cached_temperature_c = Convert_NTC_ADC_To_Temp(adc_val);
// ！！！屏蔽内鬼：在调校 TDS 期间，强制锁定 25℃ ！！！
    cached_temperature_c = 25;        
    // ---------------------------------------------------------
    // 任务 3：TDS & 缺水复用检测 - 完美防极化 + 中值滤波
    // ---------------------------------------------------------

    // a. 确保 TDSAC 引脚为推挽输出 (通常在初始化里做过了，这里加个保险)
    // 移除了抢占 PWMCON 的代码，因为 P0.2/P0.3 在新板子上本来就不做硬件 PWM
    GPIO_Init(GPIO2, GPIO_PIN_4, GPIO_MODE_OUT_PP); 
    GPIO_Init(GPIO2, GPIO_PIN_5, GPIO_MODE_OUT_PP);

    // ========================================================
    // [新增] 连续进行 10 次独立的防极化采样
    // ========================================================
			for (i = 0; i < TDS_SAMPLE_NUM; i++) {
					// 1. 正向激励
					P24 = 1;        
					P25 = 0;        
					Delay_us(800);  
					 adc_fwd = ADC_Read_Channel(1); // 读取正向ADC

					// 2. 反向去极化
					P24 = 0; 
					P25 = 1; 
					Delay_us(800);  
					 adc_rev = ADC_Read_Channel(1); // 读取反向ADC

					// 3. 彻底断电
					P24 = 0;
					P25 = 0;
					
					// 理论上 adc_fwd + adc_rev 应该约等于 4095。
					// 使用双向数据可以消除内部基准电压偏移。
					// 如果继续沿用原来的正向算法，可以用 4095 - adc_rev 来代替 adc_fwd，或者求平均：
					tds_samples[i] = (adc_fwd + (4095 - adc_rev)) / 2;
			}
    // ========================================================
    // [升级] 冒泡排序 + 去极值平均滤波
    // ========================================================
    for (i = 0; i < TDS_SAMPLE_NUM - 1; i++) {
        for (j = i + 1; j < TDS_SAMPLE_NUM; j++) {
            if (tds_samples[i] > tds_samples[j]) {
                temp_swap = tds_samples[i];
                tds_samples[i] = tds_samples[j];
                tds_samples[j] = temp_swap;
            }
        }
    }
    
    // 剔除前 2 个最小值 (下标 0, 1) 和后 2 个最大值 (下标 8, 9)
    // 取中间 6 个值求和算平均 (下标 2 到 7)
    for (i = 2; i < TDS_SAMPLE_NUM - 2; i++) {
        tds_sum += tds_samples[i];
    }
    adc_val = (uint16_t)(tds_sum / (TDS_SAMPLE_NUM - 4)); 
//		    printf("Filtered TDS ADC: %u\r\n", adc_val);

    // ========================================================
    // i. 水质判定
    // ========================================================
        // 执行带有温度补偿的水质算法
        cached_tds_ppm = Convert_ADC_To_TDS_ppm(adc_val, cached_temperature_c);       
            
        if (cached_tds_ppm < TDS_GOOD_THRESHOLD) {
            cached_water_quality = WATER_GOOD;
        } else {
            cached_water_quality = WATER_BAD;
        }
}


//低功耗部分代码
/*
 * 低功耗单次读取。
 * 返回1：成功；返回0：转换超时。
 */
static uint8_t ADC_PowerReadOnce(uint8_t channel,
                                 uint16_t *value)
{
    uint16_t timeout;

    ADCCON = 0x80 | (channel & 0x0F);
    Delay_us(20);

    ADCCON = 0xC0 | (channel & 0x0F);

    timeout = 500;

    while (!(ADCCON & 0x20)) {
        if (--timeout == 0) {
            ADCCON = 0x80 | (channel & 0x0F);
            return 0;
        }
    }

    *value = ((uint16_t)ADCVH << 4) | (ADCVL >> 4);

    ADCCON = 0x80 | (channel & 0x0F);

    return 1;
}


/*
 * 切换通道后丢弃一次，再取三次平均。
 */
static uint8_t ADC_PowerReadAverage(uint8_t channel,
                                    uint16_t *value)
{
    uint8_t i;
    uint16_t sample;
    uint16_t sum;

    if (!ADC_PowerReadOnce(channel, &sample)) {
        return 0;
    }

    sum = 0;

    for (i = 0; i < 3; i++) {
        if (!ADC_PowerReadOnce(channel, &sample)) {
            return 0;
        }

        sum += sample;
    }

    *value = sum / 3;

    return 1;
}


/*
 * 仅低功耗状态调用。
 * 不改变正常采样缓存。
 * 函数结束时关闭ADC，成功/失败路径均关闭。
 */
/* 休眠巡检只测电池，退出时关闭 ADC。 */
uint8_t HAL_ADC_PowerPoll(uint16_t *battery_mv)
{
    uint16_t battery_raw;
    uint32_t mv;

    if (!ADC_PowerReadAverage(BAT_ADC_CHANNEL, &battery_raw)) {
        ADCCON &= 0x7F;
        return 0;
    }

    ADCCON &= 0x7F;

    mv = ((uint32_t)battery_raw * VREF_MV) / ADC_RESOLUTION;
    *battery_mv = (uint16_t)(mv * VOLTAGE_DIV_RATIO);

    return 1;
}

// ==========================================
// 暴露给外部获取数据的 Getter 接口
// ==========================================
uint16_t HAL_ADC_Get_SystemVoltage_mV(void) { return cached_sys_voltage_mv; }
uint8_t HAL_ADC_Get_IsEmpty(void) {
#if DEBUG_IGNORE_WATER_EMPTY
    return 0; // 调试模式：永远欺骗系统当前满水，水泵正常运转
#else
    return WATER_EMPTY_PIN; // 正常模式：直接读取 P1.3 的瞬态物理电平
#endif
}
uint8_t  HAL_ADC_Get_WaterQuality(void)     { return cached_water_quality; }
int8_t HAL_ADC_Get_Temperature_C(void)		  { return cached_temperature_c; }
uint16_t HAL_ADC_Get_TDS_PPM(void) 					{ return cached_tds_ppm; }
uint8_t HAL_ADC_Get_Battery_SOC(void)			  { return cached_bat_soc; }