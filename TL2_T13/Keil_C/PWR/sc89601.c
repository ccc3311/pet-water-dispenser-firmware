#include "sc89601.h"
#include "SC_Init.h"
#include "sc89601.h"
#include <stdio.h>
#include "hal_uart.h"

#if APP_CHARGE_DEBUG
volatile sc_u8 g_sc89601_debug_ready = 0u;
#endif
/*
 * 用户指定连线：
 *   SC92L7533 P1.6 -> SC89601 SCL
 *   SC92L7533 P1.7 -> SC89601 SDA
 *
 * SC92L7533 端口只有“输入/强推挽输出”，没有独立开漏模式。
 * I2C 低电平：数据锁存器写 0，再切为输出。
 * I2C 高电平：切为高阻输入，由原理图上的 10k 上拉到 3.3V。
 * 禁止把引脚配置为推挽输出高电平，否则可能与从机拉低发生冲突。
 */
#define SC89601_SCL_MASK       0x40u
#define SC89601_SDA_MASK       0x80u
#define SC89601_I2C_MASK       0xC0u
#define SC89601_SCL_IO         P16
#define SC89601_SDA_IO         P17
#define SC89601_ADDR_WRITE     ((SC89601_I2C_ADDR_7BIT << 1) | 0u)
#define SC89601_ADDR_READ      ((SC89601_I2C_ADDR_7BIT << 1) | 1u)
/* SCL/SDA 的显示驱动输出位在 P1VO[4]/P1VO[5]。 */
#define SC89601_P1VO_MASK      0x30u
/* 软件 I2C 等待从机释放 SCL 的循环上限。 */
#define SC89601_SCL_WAIT_COUNT 1000u
/* 充电芯片看门狗的喂狗间隔，单位：秒。 */
#define SC89601_WDT_FEED_INTERVAL_S  10u

static sc_u8 s_sc89601_wdt_enabled = 0u;
static sc_u8 s_sc89601_wdt_seconds = 0u;

static void sc89601_i2c_delay(void)
{
    volatile sc_u8 count;

    /*
     * 兼容 4~32MHz 的保守软件延时，目标总线低于 100kHz。
     * 该延时不要求精确，只要求满足 I2C 最小时序。
     */
    count = 60u;
    while (count != 0u)
    {
        count--;
    }
}

void sc89601_delay_ms(sc_u16 ms)
{
    sc_u16 count;

    /* 仅用于上电等待和轮询节拍，不作为精确定时基准。 */
    while (ms != 0u)
    {
        count = 100u;
        while (count != 0u)
        {
            sc89601_i2c_delay();
            count--;
        }
        ms--;
    }
}

static void sc89601_scl_drive_low(void)
{
    SC89601_SCL_IO = 0;
    P1CON |= SC89601_SCL_MASK;
}

static void sc89601_scl_release(void)
{
    P1CON &= (sc_u8)(~SC89601_SCL_MASK);
}

static void sc89601_sda_drive_low(void)
{
    SC89601_SDA_IO = 0;
    P1CON |= SC89601_SDA_MASK;
}

static void sc89601_sda_release(void)
{
    P1CON &= (sc_u8)(~SC89601_SDA_MASK);
}

static sc_u8 sc89601_scl_read(void)
{
    return (SC89601_SCL_IO != 0) ? 1u : 0u;
}

static sc_u8 sc89601_sda_read(void)
{
    return (SC89601_SDA_IO != 0) ? 1u : 0u;
}

static sc_u8 sc89601_wait_scl_high(void)
{
    sc_u16 timeout;

    sc89601_scl_release();
    timeout = SC89601_SCL_WAIT_COUNT;
    while ((sc89601_scl_read() == 0u) && (timeout != 0u))
    {
        timeout--;
    }

    return (timeout == 0u) ? SC89601_ERR_SCL_TIMEOUT : SC89601_OK;
}

void sc89601_gpio_init(void)
{
    /* 关闭 P1.6/P1.7 的 ADC 输入功能，确保作为数字 GPIO 使用。 */
    ADCCFG0 &= (sc_u8)(~SC89601_I2C_MASK);

    /* 关闭这两个脚的显示驱动输出功能。 */
    P1VO &= (sc_u8)(~SC89601_P1VO_MASK);

    /* 不使用内部上拉，使用原理图上的外部 10k 上拉。 */
    P1PH &= (sc_u8)(~SC89601_I2C_MASK);

    /* 先把输出锁存值准备为 0，再释放为高阻输入。 */
    SC89601_SCL_IO = 0;
    SC89601_SDA_IO = 0;
    P1CON &= (sc_u8)(~SC89601_I2C_MASK);
}

sc_u8 sc89601_bus_recover(void)
{
    sc_u8 index;
    sc_u8 result;

    sc89601_sda_release();
    sc89601_scl_release();
    sc89601_i2c_delay();

    /* SDA 被从机占住时发送 9 个 SCL，帮助从机退出未完成字节。 */
    for (index = 0u; index < 9u; index++)
    {
        sc89601_scl_drive_low();
        sc89601_i2c_delay();

        result = sc89601_wait_scl_high();
        if (result != SC89601_OK)
        {
            return result;
        }
        sc89601_i2c_delay();
    }

    /* 生成一个 STOP：SDA 低 -> SCL 高 -> SDA 释放。 */
    sc89601_sda_drive_low();
    sc89601_i2c_delay();
    result = sc89601_wait_scl_high();
    if (result != SC89601_OK)
    {
        return result;
    }
    sc89601_i2c_delay();
    sc89601_sda_release();
    sc89601_i2c_delay();

    return (sc89601_sda_read() != 0u) ? SC89601_OK : SC89601_ERR_BUS_BUSY;
}

static sc_u8 sc89601_i2c_start(void)
{
    sc_u8 result;

    sc89601_sda_release();
    result = sc89601_wait_scl_high();
    if (result != SC89601_OK)
    {
        return result;
    }
    sc89601_i2c_delay();

    if (sc89601_sda_read() == 0u)
    {
        return SC89601_ERR_BUS_BUSY;
    }

    sc89601_sda_drive_low();
    sc89601_i2c_delay();
    sc89601_scl_drive_low();
    sc89601_i2c_delay();

    return SC89601_OK;
}

static sc_u8 sc89601_i2c_stop(void)
{
    sc_u8 result;

    sc89601_sda_drive_low();
    sc89601_i2c_delay();

    result = sc89601_wait_scl_high();
    if (result != SC89601_OK)
    {
        return result;
    }

    sc89601_i2c_delay();
    sc89601_sda_release();
    sc89601_i2c_delay();

    return SC89601_OK;
}

static sc_u8 sc89601_i2c_write_byte(sc_u8 data_value)
{
    sc_u8 bit_count;
    sc_u8 result;
    sc_u8 ack;

    for (bit_count = 0u; bit_count < 8u; bit_count++)
    {
        if ((data_value & 0x80u) != 0u)
        {
            sc89601_sda_release();
        }
        else
        {
            sc89601_sda_drive_low();
        }

        sc89601_i2c_delay();
        result = sc89601_wait_scl_high();
        if (result != SC89601_OK)
        {
            return result;
        }
        sc89601_i2c_delay();
        sc89601_scl_drive_low();
        sc89601_i2c_delay();

        data_value <<= 1;
    }

    /* 第 9 个时钟读取从机 ACK，ACK=0。 */
    sc89601_sda_release();
    sc89601_i2c_delay();
    result = sc89601_wait_scl_high();
    if (result != SC89601_OK)
    {
        return result;
    }
    sc89601_i2c_delay();
    ack = sc89601_sda_read();
    sc89601_scl_drive_low();
    sc89601_i2c_delay();

    return (ack == 0u) ? SC89601_OK : SC89601_ERR_DATA_NACK;
}

static sc_u8 sc89601_i2c_read_byte(sc_u8 *data_value, sc_u8 send_ack)
{
    sc_u8 bit_count;
    sc_u8 result;
    sc_u8 value;

    if (data_value == 0)
    {
        return SC89601_ERR_NULL;
    }

    value = 0u;
    sc89601_sda_release();

    for (bit_count = 0u; bit_count < 8u; bit_count++)
    {
        value <<= 1;
        result = sc89601_wait_scl_high();
        if (result != SC89601_OK)
        {
            return result;
        }
        sc89601_i2c_delay();
        if (sc89601_sda_read() != 0u)
        {
            value |= 0x01u;
        }
        sc89601_scl_drive_low();
        sc89601_i2c_delay();
    }

    if (send_ack != 0u)
    {
        sc89601_sda_drive_low();
    }
    else
    {
        sc89601_sda_release();
    }

    sc89601_i2c_delay();
    result = sc89601_wait_scl_high();
    if (result != SC89601_OK)
    {
        return result;
    }
    sc89601_i2c_delay();
    sc89601_scl_drive_low();
    sc89601_sda_release();
    sc89601_i2c_delay();

    *data_value = value;
    return SC89601_OK;
}

sc_u8 sc89601_write_reg(sc_u8 reg, sc_u8 data_value)
{
    sc_u8 result;

    result = sc89601_i2c_start();
    if (result != SC89601_OK)
    {
        return result;
    }

    result = sc89601_i2c_write_byte(SC89601_ADDR_WRITE);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_ADDR_NACK;
    }

    result = sc89601_i2c_write_byte(reg);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_REG_NACK;
    }

    result = sc89601_i2c_write_byte(data_value);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_DATA_NACK;
    }

    return sc89601_i2c_stop();
}

sc_u8 sc89601_read_reg(sc_u8 reg, sc_u8 *data_value)
{
    sc_u8 result;

    if (data_value == 0)
    {
        return SC89601_ERR_NULL;
    }

    result = sc89601_i2c_start();
    if (result != SC89601_OK)
    {
        return result;
    }

    result = sc89601_i2c_write_byte(SC89601_ADDR_WRITE);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_ADDR_NACK;
    }

    result = sc89601_i2c_write_byte(reg);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_REG_NACK;
    }

    /* Repeated START。 */
    result = sc89601_i2c_start();
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return result;
    }

    result = sc89601_i2c_write_byte(SC89601_ADDR_READ);
    if (result != SC89601_OK)
    {
        (void)sc89601_i2c_stop();
        return SC89601_ERR_ADDR_NACK;
    }

    /* 单字节读取后主机发送 NACK。 */
    result = sc89601_i2c_read_byte(data_value, 0u);
    (void)sc89601_i2c_stop();
    return result;
}

sc_u8 sc89601_update_bits(sc_u8 reg, sc_u8 mask, sc_u8 data_value)
{
    sc_u8 result;
    sc_u8 value;

    result = sc89601_read_reg(reg, &value);
    if (result != SC89601_OK)
    {
        return result;
    }

    value &= (sc_u8)(~mask);
    value |= (data_value & mask);
    return sc89601_write_reg(reg, value);
}

sc_u8 sc89601_check_device(sc_u8 *device_reg)
{
    sc_u8 result;
    sc_u8 value;

    if (device_reg == 0)
    {
        return SC89601_ERR_NULL;
    }

    result = sc89601_read_reg(SC89601_REG_DEVICE, &value);
    if (result != SC89601_OK)
    {
        return result;
    }

    *device_reg = value;
    if (((value & 0x78u) >> 3) != 3u)
    {
        return SC89601_ERR_DEVICE_ID;
    }

    return SC89601_OK;
}

sc_u8 sc89601_reset_registers(void)
{
    sc_u8 result;

    /* 对应官方 sc8960x_reset_chip()：REG0B.REG_RST 写 1 后自清零。 */
    result = sc89601_update_bits(SC89601_REG_DEVICE, 0x80u, 0x80u);
    if (result == SC89601_OK)
    {
        sc89601_delay_ms(2u);
    }

    return result;
}

sc_u8 sc89601_set_input_current_limit(sc_u16 current_ma)
{
    sc_u8 code_value;

    if (current_ma < 100u)
    {
        current_ma = 100u;
    }
    else if (current_ma > 3200u)
    {
        current_ma = 3200u;
    }

    code_value = (sc_u8)((current_ma - 100u) / 100u);
    return sc89601_update_bits(SC89601_REG_IINDPM, 0x1Fu, code_value);
}

sc_u8 sc89601_set_input_voltage_limit(sc_u16 voltage_mv)
{
    sc_u8 code_value;

    if (voltage_mv <= 3900u)
    {
        code_value = 0u;
    }
    else if (voltage_mv <= 5100u)
    {
        code_value = (sc_u8)((voltage_mv - 3900u) / 100u);
    }
    else if (voltage_mv < 8000u)
    {
        /* 5.2~7.9V 没有编码，向下钳位到 5.1V。 */
        code_value = 12u;
    }
    else if (voltage_mv < 8200u)
    {
        code_value = 13u;
    }
    else if (voltage_mv < 8400u)
    {
        code_value = 14u;
    }
    else
    {
        code_value = 15u;
    }

    return sc89601_update_bits(SC89601_REG_VINDPM_BOOSTV_OVP,
                               0x0Fu,
                               code_value);
}

sc_u8 sc89601_set_charge_current(sc_u16 current_ma)
{
    sc_u8 code_value;

    if (current_ma > 3000u)
    {
        current_ma = 3000u;
    }

    code_value = (sc_u8)(current_ma / 60u);
    return sc89601_update_bits(SC89601_REG_ICC, 0xBFu,(sc_u8)(0x80u | code_value));
}

sc_u8 sc89601_set_charge_voltage(sc_u16 voltage_mv)
{
    sc_u8 code_value;

    if (voltage_mv < 3848u)
    {
        voltage_mv = 3848u;
    }
    else if (voltage_mv > 4840u)
    {
        /* REG04 五位粗调的最大值；REG0D 细调不在最小验证中使用。 */
        voltage_mv = 4840u;
    }

    /* 向下量化，避免实际设定高于调用值。 */
    code_value = (sc_u8)((voltage_mv - 3848u) / 32u);
    return sc89601_update_bits(SC89601_REG_VBAT_REG,
                               0xF8u,
                               (sc_u8)(code_value << 3));
}

sc_u8 sc89601_set_precharge_current(sc_u16 current_ma)
{
    sc_u8 code_value;

    if (current_ma < 60u)
    {
        current_ma = 60u;
    }
    else if (current_ma > 960u)
    {
        current_ma = 960u;
    }

    code_value = (sc_u8)((current_ma - 60u) / 60u);
    return sc89601_update_bits(SC89601_REG_ITC_ITERM,
                               0xF0u,
                               (sc_u8)(code_value << 4));
}

sc_u8 sc89601_set_termination_current(sc_u16 current_ma)
{
    sc_u8 code_value;

    if (current_ma < 60u)
    {
        current_ma = 60u;
    }
    else if (current_ma > 960u)
    {
        current_ma = 960u;
    }

    code_value = (sc_u8)((current_ma - 60u) / 60u);
    return sc89601_update_bits(SC89601_REG_ITC_ITERM, 0x0Fu, code_value);
}

sc_u8 sc89601_set_acovp_threshold(sc_u16 voltage_mv)
{
    sc_u8 code_value;

    if (voltage_mv <= 5800u)
    {
        code_value = 0u;
    }
    else if (voltage_mv <= 6400u)
    {
        code_value = 1u;
    }
    else if (voltage_mv <= 11000u)
    {
        code_value = 2u;
    }
    else
    {
        code_value = 3u;
    }

    return sc89601_update_bits(SC89601_REG_VINDPM_BOOSTV_OVP,
                               0xC0u,
                               (sc_u8)(code_value << 6));
}

sc_u8 sc89601_set_vindpm_track(sc_u16 offset_mv)
{
    sc_u8 code_value;

    if (offset_mv == 0u)
    {
        code_value = 0u;
    }
    else if (offset_mv <= 200u)
    {
        code_value = 1u;
    }
    else if (offset_mv <= 250u)
    {
        code_value = 2u;
    }
    else
    {
        code_value = 3u;
    }

    return sc89601_update_bits(SC89601_REG_CTL3, 0x03u, code_value);
}

sc_u8 sc89601_set_charge_enable(sc_u8 enable)
{
    return sc89601_update_bits(SC89601_REG_CTL1,
                               0x10u,
                               (enable != 0u) ? 0x10u : 0x00u);
}

/* 每行：寄存器、有效位掩码、目标值。 */
static const sc_u8 code s_sc89601_verify_table[][3] = {
    {0x00u, 0xFFu, 0x1Bu},
    {0x01u, 0xBFu, 0x18u}, /* 忽略WD_RST命令位 */
    {0x02u, 0xBFu, 0x8Fu},
    {0x03u, 0xFFu, 0x42u}, /* 预充300mA，终止180mA */
    {0x04u, 0xFFu, 0x5Eu},
    {0x05u, 0xBFu, 0x9Du},
    {0x06u, 0xFFu, 0xFFu},
    {0x07u, 0x7Fu, 0x44u}, /* 忽略输入检测命令位 */
    {0x0Cu, 0xFFu, 0x35u},
    {0x0Du, 0xFFu, 0x24u},
    {0x0Eu, 0xBFu, 0xB2u}   /* 同步1MHz配置，仍忽略输入检测状态bit6。*/
};

static sc_u8 sc89601_verify_minimal_config(void)
{
    sc_u8 index;
    sc_u8 result;
    sc_u8 value;

    for (index = 0u;
         index < sizeof(s_sc89601_verify_table) /
                 sizeof(s_sc89601_verify_table[0]);
         index++) {

        result = sc89601_read_reg(
            s_sc89601_verify_table[index][0], &value);

        if (result != SC89601_OK) {
            return result;
        }

        if ((value & s_sc89601_verify_table[index][1]) !=
            s_sc89601_verify_table[index][2]) {
            return SC89601_ERR_VERIFY;
        }
    }

    return SC89601_OK;
}

/*
 * 读取芯片寄存器，保存原始数据，并解析电源、充电和故障状态。
 * snapshot指向调用者提供的结构体，函数直接更新该结构体。
 *
 * 返回OK表示本轮读取和解析完成；中途失败时，快照可能仅部分更新，
 * 调用者应先检查返回值，再根据快照进行业务判断。
 */
sc_u8 sc89601_read_snapshot(volatile sc89601_snapshot_t *snapshot)
{
    sc_u8 result;
    sc_u8 reg;
    sc_u8 value;

    /* 未提供快照存储对象，不能继续访问其成员。 */
    if (snapshot == 0)
    {
        return SC89601_ERR_NULL;
    }

    /*
     * 按地址逐个读取寄存器，保存到MCU内的reg[]数组。
     * REG09具有故障锁存读取特性，留到后面单独连续读取。
     */
    for (reg = 0u; reg < SC89601_REG_COUNT; reg++)
    {
        if (reg == SC89601_REG_FAULT)
        {
            continue;  /* 仅跳过当前地址，继续读取后续寄存器。 */
        }

        /*
         * result接收操作结果；寄存器读回的字节通过&value输出。
         * 读取失败立即返回，交给调用者处理通信异常。
         */
        result = sc89601_read_reg(reg, &value);
        if (result != SC89601_OK)
        {
            return result;
        }

        /* 保存原始字节，供后面的字段解析和上层配置检查使用。 */
        snapshot->reg[reg] = value;
    }

    /*
     * REG09连续读取两次：
     * 第一次保存此前锁存的故障状态，不代表故障此刻仍然存在。
     * 第二次取得当前故障状态。
     */
    result = sc89601_read_reg(SC89601_REG_FAULT, &value);
    if (result != SC89601_OK)
    {
        return result;
    }
    snapshot->fault_latched = value;

    result = sc89601_read_reg(SC89601_REG_FAULT, &value);
    if (result != SC89601_OK)
    {
        return result;
    }
    snapshot->fault_current = value;

    /* 原始寄存器数组也保存第二次读到的当前故障值。 */
    snapshot->reg[SC89601_REG_FAULT] = value;

    /*
     * 以下从已保存的原始字节中提取字段，不再进行I2C读取。
     * &保留目标位，右移则将字段移到最低位，便于上层比较。
     */

    /* DEVICE：bit6~3为芯片型号编码，bit1~0为版本编码。 */
    value = snapshot->reg[SC89601_REG_DEVICE];
    snapshot->part_number = (value & 0x78u) >> 3;
    snapshot->revision = value & 0x03u;

    value = snapshot->reg[SC89601_REG_STAT1];

    /* STAT1 bit7~5：输入源状态编码，0表示无输入。 */
    snapshot->vbus_status = (value & 0xE0u) >> 5;

    /*
     * STAT1 bit4~3：充电状态编码。
     * 0=未充电，1=小电流充电，2=快充，3=充电终止。
     */
    snapshot->charge_status = (value & 0x18u) >> 3;

    /* STAT1 bit2：输入电源是否就绪，0=未就绪，1=就绪。 */
    snapshot->power_good = (value & 0x04u) >> 2;

    /*
     * STAT2 bit7：VBUS接入标志。
     * 上层RUNNING分支根据此字段判断是否转入等待输入。
     */
    value = snapshot->reg[SC89601_REG_STAT2];
    snapshot->vbus_good = (value & 0x80u) >> 7;

    /* CTL5 bit6：INPUT_DET_DONE，手册标为DP/DM识别完成状态。 */
    value = snapshot->reg[SC89601_REG_CTL5];
    snapshot->input_det_done = (value & 0x40u) >> 6;

    return SC89601_OK;
}

#define SC89601_STEP_WAIT_INPUT  6u
#define SC89601_STEP_RUNNING     17u

volatile sc_u8  g_sc89601_init_result = SC89601_PENDING;
volatile sc_u8  g_sc89601_poll_result = SC89601_PENDING;
volatile sc89601_snapshot_t g_sc89601_snapshot;

static sc_u8 s_sc89601_step = 0u;
static sc_u8 s_sc89601_wait_ticks = 0u;
static sc_u8 s_sc89601_input_absent = 0u;

/* 失败后释放主循环，约5秒后重新初始化。 */
static void sc89601_service_failed(sc_u8 result)
{
#if APP_CHARGE_DEBUG
    g_sc89601_debug_ready = 0u; /* 进入重试后，不再把旧快照标为新数据。 */
#endif
    g_sc89601_init_result = result;

    s_sc89601_step = 0u;
    s_sc89601_wait_ticks = 49u;
    s_sc89601_input_absent = 0u;

    s_sc89601_wdt_enabled = 0u;
    s_sc89601_wdt_seconds = 0u;
}

/* 正常运行和STOP巡检共用；只能在主循环中调用。 */
void sc89601_tick_1s(void)
{
    sc_u8 result;
    sc_u8 value;

    if (!s_sc89601_wdt_enabled) {
        return;
    }

    if (++s_sc89601_wdt_seconds <
        SC89601_WDT_FEED_INTERVAL_S) {
        return;
    }

    /*
     * 先检查配置，再喂狗。
     * R05目标为9D；看门狗复位后的默认9F需要重新配置。
     * 忽略保留位bit6。
     */
    result = sc89601_read_reg(SC89601_REG_CTL2, &value);

    if (result == SC89601_OK) {
        if ((value & 0xBFu) != 0x9Du) {
            result = SC89601_ERR_VERIFY;
        }
    }

    if (result == SC89601_OK) {
        /* 置WD_RST，保留当前充电状态。 */
        result = sc89601_update_bits(
            SC89601_REG_CTL1, 0x40u, 0x40u);
    }

    if (result == SC89601_OK) {
        s_sc89601_wdt_seconds = 0u;
        return;
    }

    /* 通信或配置异常时，尽力关闭充电和OTG。 */
    (void)sc89601_update_bits(
        SC89601_REG_CTL1, 0x30u, 0x00u);

    sc89601_service_failed(result);
}

/* 仅在整机启动时调用，STOP 返回后不重复初始化。 */
void sc89601_service_init(void)
{
    sc_u8 reg;

    sc89601_gpio_init();

    s_sc89601_step = 0u;
    s_sc89601_wait_ticks = 1u;
    s_sc89601_input_absent = 0u;
		s_sc89601_wdt_enabled = 0u;
		s_sc89601_wdt_seconds = 0u;
	
    g_sc89601_init_result = SC89601_PENDING;
    g_sc89601_poll_result = SC89601_PENDING;

    for (reg = 0u; reg < SC89601_REG_COUNT; reg++) {
        g_sc89601_snapshot.reg[reg] = 0u;
    }

    g_sc89601_snapshot.fault_latched = 0u;
    g_sc89601_snapshot.fault_current = 0u;
    g_sc89601_snapshot.part_number = 0u;
    g_sc89601_snapshot.revision = 0u;
    g_sc89601_snapshot.vbus_status = 0u;
    g_sc89601_snapshot.charge_status = 0u;
    g_sc89601_snapshot.power_good = 0u;
    g_sc89601_snapshot.vbus_good = 0u;
    g_sc89601_snapshot.input_det_done = 0u;
}

/*
 * 正常运行期间每 100ms 调用。
 * 输入识别分次检查，保留主循环处理按键、串口和喂狗的机会。
 */
void sc89601_service_100ms(void)
{
    sc_u8 result;
    sc_u8 device_reg;
    sc_u8 reg08;
    sc_u8 reg0a;

    if (s_sc89601_wait_ticks != 0u) {
        s_sc89601_wait_ticks--;
        return;
    }

    result = SC89601_OK;

    switch (s_sc89601_step) {
        case 0u:
            g_sc89601_init_result = SC89601_PENDING;
            g_sc89601_poll_result = SC89601_PENDING;
            result = sc89601_bus_recover();
            break;

        case 1u:
            result = sc89601_check_device(&device_reg);
            if (result == SC89601_OK) {
                g_sc89601_snapshot.reg[SC89601_REG_DEVICE] =
                    device_reg;
                g_sc89601_snapshot.part_number =
                    (device_reg & 0x78u) >> 3;
                g_sc89601_snapshot.revision =
                    device_reg & 0x03u;
            }
            break;

case 2u:
    s_sc89601_wdt_enabled = 0u;
    s_sc89601_wdt_seconds = 0u;

    result = sc89601_reset_registers();
    break;

case 3u:
    /* SYS_MIN=3.4V，配置期间关闭充电和OTG。 */
    result = sc89601_write_reg(
        SC89601_REG_CTL1, 0x08u);
        break;

        case 4u:
            result = sc89601_update_bits(
                SC89601_REG_IINDPM, 0x80u, 0x00u);
            break;

case 5u:
    /* 必须先建立12V输入配置，再等待输入识别。 */
    result = sc89601_write_reg(
        SC89601_REG_VINDPM_BOOSTV_OVP, 0xFFu);
    if (result != SC89601_OK) {
        break;
    }

    /* 40秒看门狗，其余位匹配目标表，保留bit6。 */
    result = sc89601_update_bits(
        SC89601_REG_CTL2, 0xBFu, 0x9Du);
    if (result != SC89601_OK) {
        break;
    }

    /* 立即喂一次，建立软件计时起点。 */
    result = sc89601_update_bits(
        SC89601_REG_CTL1, 0x40u, 0x40u);

    if (result == SC89601_OK) {
        s_sc89601_wdt_enabled = 1u;
        s_sc89601_wdt_seconds = 0u;
    }
    break;

        case SC89601_STEP_WAIT_INPUT:
#if APP_CHARGE_DEBUG
            /* 打印模式读取完整快照，未插电时也能观察各寄存器。 */
            result = sc89601_read_snapshot(&g_sc89601_snapshot);

            g_sc89601_debug_ready =
                (result == SC89601_OK) ? 1u : 0u;

            if (result != SC89601_OK) {
                break;
            }

            reg08 = g_sc89601_snapshot.reg[SC89601_REG_STAT1];
            reg0a = g_sc89601_snapshot.reg[SC89601_REG_STAT2];
#else
            result = sc89601_read_reg(
                SC89601_REG_STAT2, &reg0a);
            if (result != SC89601_OK) {
                break;
            }

            result = sc89601_read_reg(
                SC89601_REG_STAT1, &reg08);
            if (result != SC89601_OK) {
                break;
            }
#endif

            g_sc89601_snapshot.reg[SC89601_REG_STAT1] = reg08;
            g_sc89601_snapshot.reg[SC89601_REG_STAT2] = reg0a;
            g_sc89601_snapshot.vbus_status =
                (reg08 >> 5) & 0x07u;
            g_sc89601_snapshot.charge_status =
                (reg08 >> 3) & 0x03u;
            g_sc89601_snapshot.power_good =
                (reg08 >> 2) & 0x01u;
            g_sc89601_snapshot.vbus_good =
                (reg0a >> 7) & 0x01u;

            s_sc89601_input_absent =
                ((reg0a & 0x80u) == 0u) ? 1u : 0u;

            /*
             * 沿用最小板验证条件。
             * 未满足时约 500ms 后再查，不复位寄存器。
             */
            if (((reg0a & 0x80u) == 0u) ||
                ((reg08 & 0x04u) == 0u) ||
                ((reg08 & 0xE0u) == 0u)) {
                s_sc89601_wait_ticks = 4u;
                return;
            }
            break;

        case 7u:
            result = sc89601_set_input_voltage_limit(
                SC89601_TEST_VINDPM_MV);
            break;

        case 8u:
            result = sc89601_set_acovp_threshold(
                SC89601_TEST_ACOVP_MV);
            break;

        case 9u:
            result = sc89601_set_input_current_limit(
                SC89601_TEST_IINDPM_MA);
            break;

        case 10u:
            result = sc89601_set_vindpm_track(
                SC89601_TEST_VINDPM_TRACK_MV);
            break;

        case 11u:
            result = sc89601_set_precharge_current(
                SC89601_TEST_IPRECHG_MA);
            break;

        case 12u:
            result = sc89601_set_termination_current(
                SC89601_TEST_ITERM_MA);
            break;

case 13u:
    result = sc89601_set_charge_voltage(
        SC89601_TEST_VREG_MV);

    if (result == SC89601_OK) {
        /* 补充充电45分钟，回充压差100mV。 */
        result = sc89601_update_bits(
            SC89601_REG_VBAT_REG, 0x07u, 0x06u);
    }
    break;

        case 14u:
            result = sc89601_set_charge_current(
                SC89601_TEST_ICHG_MA);
            break;

        case 15u:
            result = sc89601_set_charge_enable(1u);
            break;

        case 16u:
            result = sc89601_verify_minimal_config();
            break;

        case SC89601_STEP_RUNNING:
            result = sc89601_read_snapshot(
                &g_sc89601_snapshot);
#if APP_CHARGE_DEBUG
            /* 正常巡检完成后，通知打印任务取走完整快照。 */
            g_sc89601_debug_ready =
                (result == SC89601_OK) ? 1u : 0u;
#endif
            g_sc89601_poll_result = result;

            if (result != SC89601_OK) {
                break;
            }
						/* 看门狗超时或配置丢失，进入重新初始化流程。 */
						if ((g_sc89601_snapshot.fault_current & 0x80u) ||
								((g_sc89601_snapshot.reg[SC89601_REG_CTL2] &
									0xBFu) != 0x9Du)) {

								(void)sc89601_update_bits(
										SC89601_REG_CTL1, 0x30u, 0x00u);

								sc89601_service_failed(SC89601_ERR_VERIFY);
								return;
						}
            if (g_sc89601_snapshot.vbus_good == 0u) {
                /*拔电后等待新的输入识别。
                 * 再次插电时重新应用参数，不在此复位芯片。*/
                s_sc89601_input_absent = 1u;
                s_sc89601_step = SC89601_STEP_WAIT_INPUT;
                s_sc89601_wait_ticks = 4u;
                g_sc89601_init_result = SC89601_PENDING;
            } else {
                s_sc89601_input_absent = 0u;
                s_sc89601_wait_ticks = 9u;
            }
            return;

        default:
            s_sc89601_step = 0u;
            s_sc89601_input_absent = 0u;
            return;
    }

    if (result != SC89601_OK) {
        /* 参数配置失败时，尽力关闭充电，保存原始错误码。 */
        if ((s_sc89601_step >= 7u) &&
            (s_sc89601_step <= 16u)) {
            (void)sc89601_set_charge_enable(0u);
        }

        sc89601_service_failed(result);
        return;
    }

    s_sc89601_step++;

    if (s_sc89601_step == SC89601_STEP_RUNNING) {
        g_sc89601_init_result = SC89601_OK;
    }
}

/* 配置过程保持运行；确认无外部输入后才允许 STOP。 */
sc_u8 sc89601_can_stop(void)
{
    return ((s_sc89601_step == SC89601_STEP_WAIT_INPUT) &&
            s_sc89601_input_absent) ? 1u : 0u;
}

/*
 * 仅在休眠巡检中调用。
 * 通过寄存器识别插电，不依赖尚未有效的 Type-C IO。
 */
sc_u8 sc89601_sleep_wake_required(void)
{
    sc_u8 result;
    sc_u8 reg0a;
/* 秒任务发现异常后，必须退出STOP才能执行初始化重试。 */
if (!sc89601_can_stop()) {
    return 1u;
}
    result = sc89601_read_reg(SC89601_REG_STAT2, &reg0a);
    if (result != SC89601_OK) {
        sc89601_service_failed(result);
        return 1u;
    }

    if ((reg0a & 0x80u) != 0u) {
        s_sc89601_input_absent = 0u;
        s_sc89601_wait_ticks = 0u;
        return 1u;
    }

    return 0u;
}

/* 旧驱动没有OTG功能；此接口仅用于当前工程的充电对照测试。 */
void sc89601_set_otg_allowed(sc_u8 allowed)
{
    (void)allowed; /* 接收调用，不执行升压操作。 */
}

sc_u8 sc89601_otg_is_active(void)
{
    return 0u; /* 旧驱动未启动OTG。 */
}

sc_u8 sc89601_fault_is_latched(void)
{
    return 0u; /* 旧驱动采用延时重试，没有故障锁定状态。 */
}
