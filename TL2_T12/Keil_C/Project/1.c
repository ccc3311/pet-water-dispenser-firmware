/*
 * SC89601 充电 / OTG 升压驱动（本版只补充说明，执行逻辑与参数不变）。
 * 
 * 主程序调用顺序：
 * 1. 上电调用一次 sc89601_service_init()，建立 GPIO 和软件状态。
 * 2. 正常运行每100ms：先sc89601_set_otg_allowed()，再sc89601_service_100ms()。
 *    当前业务在电压已有采样且电量不是低档时允许 OTG；软件关机不单独禁止升压。
 * 3. BTM秒任务调用sc89601_tick_1s()，正常运行与MCU STOP巡检期间都要调用。
 * 4. STOP巡检调用sc89601_sleep_wake_required()；返回1后主程序恢复正常调度。
 * 
 * 模式选择：外部输入优先；无外部输入且业务允许时，电池 OTG 输出目标为 4.6V。
 * 有外部输入时先退出 OTG，输入识别有效后分步配置并使能充电。
 * 充电恒流、恒压和终止过程由芯片硬件执行，MCU 负责配置、巡检和喂狗。
 * 
 * 当前充电目标：输入限流 2800mA、VINDPM 8.4V、输入过压阈值 14.2V；
 * 充电 600mA / 4.2V、预充 300mA、终止 180mA，芯片看门狗 40 秒。
 * 芯片看门狗与 MCU 自身的 WDT 是两套机制；本文件只维护 SC89601 看门狗。
 * 
 * 硬件边界：SYS_MIN 调节的是 VSYS；它不会把适配器输入下的 PMID 固定为 4.6V。
 * 水泵最终供电电压由外部电源电路保证，水泵开关仍由原 HAL/FSM 控制。
 * 软件 I2C 和日志会占用主循环时间，所以本文件接口不放入中断中执行。
 */

#include "sc89601.h"
#include "SC_Init.h"
#include "sc89601.h"
#include <stdio.h>

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
/* 等待SCL释放的循环上限；1000是循环次数，不是毫秒数。 */
#define SC89601_SCL_WAIT_COUNT 1000u
/* 芯片看门狗配置为40秒；每10个BTM秒节拍尝试喂一次，留出时间余量。 */
#define SC89601_WDT_FEED_INTERVAL_S  10u

/* 软件维护许可：只有芯片看门狗已配置且首次喂狗成功，才开始周期维护。 */
static sc_u8 s_sc89601_wdt_enabled = 0u;
/* 距离上次成功喂狗的BTM秒节拍计数；它不是芯片内部的倒计时寄存器。 */
static sc_u8 s_sc89601_wdt_seconds = 0u;

/*
 * 软件I2C的短延时，供每个SCL/SDA边沿使用。
 * volatile防止空循环被完全消除；实际总线速度仍取决于时钟和编译选项。
 */
static void sc89601_i2c_delay(void)
{
    volatile sc_u8 count;

    /*
     * 沿用现有延时计数，设计目标为低于100kHz的保守软件I2C时序。
     * 该延时不要求精确，只要求满足 I2C 最小时序。
     */
    count = 60u;
    while (count != 0u)
    {
        count--;
    }
}

/*
 * 粗略等待接口，当前用于寄存器复位后的短暂等待。
 * ms表示名义等待量；不用它驱动业务秒时钟或看门狗计时。
 */
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

/* 模拟开漏拉低SCL：先写0，再设为输出，避免切换方向时短暂输出高电平。 */
static void sc89601_scl_drive_low(void)
{
    SC89601_SCL_IO = 0;
    /* P1CON对应位为1表示输出；这里只修改P1.6。 */
    P1CON |= SC89601_SCL_MASK;
}

/* 释放SCL：P1.6切为输入，由外部上拉或从机决定电平，不主动输出1。 */
static void sc89601_scl_release(void)
{
    P1CON &= (sc_u8)(~SC89601_SCL_MASK);
}

/* 模拟开漏拉低SDA：先写0，再把P1.7切为输出。 */
static void sc89601_sda_drive_low(void)
{
    SC89601_SDA_IO = 0;
    P1CON |= SC89601_SDA_MASK;
}

/* 释放SDA为高阻输入，允许从机输出数据或ACK。 */
static void sc89601_sda_release(void)
{
    P1CON &= (sc_u8)(~SC89601_SDA_MASK);
}

/* 读取引脚实际电平；释放SCL后，用它判断总线是否真正变高。 */
static sc_u8 sc89601_scl_read(void)
{
    return (SC89601_SCL_IO != 0) ? 1u : 0u;
}

/* 读取SDA实际电平，用于接收数据、判断ACK及检查总线占用。 */
static sc_u8 sc89601_sda_read(void)
{
    return (SC89601_SDA_IO != 0) ? 1u : 0u;
}

/*
 * 释放SCL并有界等待高电平，兼顾从机拉低SCL的时钟延展。
 * 持续拉低则返回超时，避免软件I2C永久阻塞主循环。
 */
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

/*
 * 初始化软件I2C引脚，屏蔽这两个脚的模拟输入及显示输出复用。
 * 只配置P1.6/P1.7相关位，保留同组端口上其他外设的设置。
 */
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

/*
 * 总线恢复：发送9个时钟，再产生STOP，尝试结束从机未完成的传输。
 * 恢复后仍检查SDA；物理短路或持续被拉低时返回错误，不能靠时钟恢复。
 */
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

/* 产生START：SCL为高时SDA由高变低；随后拉低SCL，准备发送第一位。 */
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

    /* 开始前SDA应为高；低电平说明总线仍被占用。 */
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

/* 产生STOP：保持SDA低，等待SCL高，再释放SDA，使SDA在SCL高时上升。 */
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

/*
 * 按最高位在前发送8位，第9个时钟释放SDA并读取从机ACK。
 * 发送1使用高阻释放，发送0使用拉低，避免与从机争用。
 */
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

/*
 * 主机释放SDA，让从机发送8位，并在SCL高电平期间采样。
 * send_ack非0：第9个时钟主机拉低表示继续读；为0：发送NACK表示结束。
 */
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

    /* 第9个时钟由主机决定ACK/NACK，单寄存器读取使用NACK。 */
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

/*
 * 一次寄存器写事务：START -> 写地址0xD6 -> 寄存器地址 -> 数据 -> STOP。
 * 地址/寄存器/数据阶段失败会映射为对应阶段的NACK错误；
 * 这些阶段码定位失败位置，也可能包含底层SCL等待超时，不仅代表没收到ACK。
 */
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

/*
 * 一次寄存器读事务：先写寄存器地址，再重复START，使用读地址0xD7。
 * 读1字节后发送NACK和STOP；成功返回也要求STOP流程没有SCL超时。
 */
sc_u8 sc89601_read_reg(sc_u8 reg, sc_u8 *data_value)
{
    sc_u8 result;

    if (data_value == 0)
    {
        /* 空指针不能用于接收寄存器数据。 */
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

    /* 单字节读取后发送NACK，并检查事务是否正常结束。 */
    result = sc89601_i2c_read_byte(data_value, 0u);
    if (result != SC89601_OK) {
        (void)sc89601_i2c_stop();
        return result;
    }

    return sc89601_i2c_stop();
}

/*
 * 通用读改写：mask中为1的位参与修改，其余位保留当前寄存器值。
 * 例如R01的mask=0x40仅发出喂狗命令，不改变充电/OTG使能位。
 */
sc_u8 sc89601_update_bits(sc_u8 reg, sc_u8 mask, sc_u8 data_value)
{
    sc_u8 result;
    sc_u8 value;

    result = sc89601_read_reg(reg, &value);
    if (result != SC89601_OK)
    {
        return result;
    }

    /* 先清目标位，保留mask之外的原值。 */
    value &= (sc_u8)(~mask);
    /* 只把data_value中属于mask的位写入，避免改到其他功能。 */
    value |= (data_value & mask);
    return sc89601_write_reg(reg, value);
}

/* 读REG0B的PN[6:3]，要求为3；用于确认当前驱动对应的芯片型号。 */
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

/*
 * 对REG0B的REG_RST(bit7)写1，触发芯片寄存器复位，该命令位自清。
 * 这不是MCU复位；复位后还要由service状态机重新建立目标参数。
 */
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

/*
 * 设置REG00.IINDPM[4:0]：100mA起点，每档增加100mA，范围100~3200mA。
 * 2800mA得到代码27(0x1B)；mask=0x1F保留EN_HIZ和STAT引脚配置。
 */
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

    /* 整数除法向下取档；配置输入限流不等于实际始终吸取该电流。 */
    code_value = (sc_u8)((current_ma - 100u) / 100u);
    return sc89601_update_bits(SC89601_REG_IINDPM, 0x1Fu, code_value);
}

/*
 * 设置REG06.VINDPM[3:0]，表示输入电压动态管理阈值，不是输出电压。
 * 代码0~12为3.9~5.1V；13/14/15分别为8.0/8.2/8.4V。
 * mask=0x0F仅修改低4位，保留输入过压阈值及BOOSTV中间两位。
 */
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

/*
 * 设置REG02：ICHG[5:0]每档60mA；600mA的代码为0x0A。
 * 本函数还把bit7的BOOST_LIM置1，选择1.2A升压电流限制，故目标R02=0x8A。
 * mask=0xBF保留bit6；此函数只配置参数，不单独启动充电。
 */
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

/*
 * 设置REG04.VBAT_REG[7:3]的粗调值：3848mV起点，每档32mV。
 * 4200mV得到代码11；左移3位为0x58，REG04低3位另行设置。
 * 这里设置电池充电电压，与PMID水泵电源电压属于不同节点。
 */
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

/*
 * 设置REG03高4位的预充电流：60mA起点，每档60mA。
 * 300mA得到代码4，写成高半字节0x40；低半字节终止电流保持不变。
 */
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

/*
 * 设置REG03低4位的终止电流：60mA起点，每档60mA。
 * 沿用180mA得到代码2，配合预充300mA，最终R03=0x42。
 * 终止电流是芯片判断充电结束的条件之一，不是恒流阶段的充电电流。
 */
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

/*
 * 设置REG06.ACOVP[7:6]：5.8/6.4/11.0/14.2V四个输入过压阈值。
 * 当前14.2V选代码3，写入0xC0，适配12V外部输入；保留寄存器低6位。
 */
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

/*
 * 设置REG07.VINDPM_TRACK[1:0]：关闭，或按电池电压加200/250/300mV跟踪。
 * 当前目标为0，使用已设定的VINDPM，保留REG07其他功能位。
 */
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

/*
 * 只修改REG01.CHG_CFG(bit4)，非0开启充电、0关闭。
 * 它不负责清除OTG_CFG；模式切换先由otg_leave或配置流程关闭OTG。
 */
sc_u8 sc89601_set_charge_enable(sc_u8 enable)
{
    return sc89601_update_bits(SC89601_REG_CTL1,
                               0x10u,
                               (enable != 0u) ? 0x10u : 0x00u);
}

/*
 * 充电配置校验表：每行依次为寄存器、校验掩码、掩码后的目标值。
 * Keil C51的code限定把表存于程序存储区；这张表校验的是充电模式。
 * 命令自清位、输入检测状态位或保留位不参与稳定值比较。
 * 目标值是固定表；以后调整头文件参数时，需同步调整对应校验值。
 */
static const sc_u8 code s_sc89601_verify_table[][3] = {
    {0x00u, 0xFFu, 0x1Bu}, /* HIZ关闭，输入限流2800mA */
    {0x01u, 0xBFu, 0x18u}, /* 充电开、OTG关、SYS_MIN=3.4V；忽略bit6喂狗命令 */
    {0x02u, 0xBFu, 0x8Au}, /* 升压限流1.2A，充电600mA；忽略保留位bit6 */
    {0x03u, 0xFFu, 0x42u}, /* 预充300mA，终止180mA */
    {0x04u, 0xFFu, 0x5Eu}, /* 4.2V、Top-off补充充电45分钟、回充压差100mV */
    {0x05u, 0xBFu, 0x9Du}, /* 终止使能、40秒WDT、安全计时使能/10小时；忽略bit6 */
    {0x06u, 0xFFu, 0xFFu}, /* 输入过压14.2V、BOOST中间两位=11、VINDPM=8.4V */
    {0x07u, 0x7Fu, 0x44u}, /* VINDPM跟踪关闭；忽略FORCE_DET命令位bit7 */
    {0x0Cu, 0xFFu, 0x35u}, /* 沿用JEITA温度策略配置 */
    {0x0Du, 0xFFu, 0x24u}, /* 沿用充电模式CTL4；OTG模式另设为0x22 */
    {0x0Eu, 0xBFu, 0xB2u}  /* 沿用CTL5；忽略INPUT_DET_DONE状态位bit6 */
};

/*
 * 逐项读回充电配置；I2C读失败返回通信错误，目标位不匹配返回VERIFY(8)。
 * 校验通过说明这些寄存器匹配目标；不能替代输出电压和实际充电电流测量。
 */
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
 * 读REG00~REG0E并解析状态，供日志与业务读取缓存。
 * REG09单独连读两次；读取会消费故障锁存，所以保存第一读与第二读的区别。
 * 这些寄存器是依次读取的，缓存不是所有硬件状态在同一瞬间的快照。
 * 中途失败会返回；此时缓存可能只有前面部分得到更新。
 */
sc_u8 sc89601_read_snapshot(volatile sc89601_snapshot_t *snapshot)
{
    sc_u8 result;
    sc_u8 reg;
    sc_u8 value;

    if (snapshot == 0)
    {
        return SC89601_ERR_NULL;
    }

    for (reg = 0u; reg < SC89601_REG_COUNT; reg++)
    {
        if (reg == SC89601_REG_FAULT)
        {
            continue;
        }

        result = sc89601_read_reg(reg, &value);
        if (result != SC89601_OK)
        {
            return result;
        }
        snapshot->reg[reg] = value;
    }

    /* 手册要求 REG09 连续读取两次：第一次为既有锁存，第二次为当前故障。 */
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
    snapshot->reg[SC89601_REG_FAULT] = value;

    value = snapshot->reg[SC89601_REG_DEVICE];
    snapshot->part_number = (value & 0x78u) >> 3;
    snapshot->revision = value & 0x03u;

    /*
     * REG08：VBUS_STAT[7:5]为输入/OTG类型；CHRG_STAT[4:3]为充电阶段。
     * charge_status：0未充电、1预充、2快速充电、3充电完成；PG_STAT为bit2。
     */
    value = snapshot->reg[SC89601_REG_STAT1];
    snapshot->vbus_status = (value & 0xE0u) >> 5;
    snapshot->charge_status = (value & 0x18u) >> 3;
    snapshot->power_good = (value & 0x04u) >> 2;

    /* REG0A.bit7为VBUS_GD；模式选择还要结合REG08和P50，不能只看这一位。 */
    value = snapshot->reg[SC89601_REG_STAT2];
    snapshot->vbus_good = (value & 0x80u) >> 7;

    value = snapshot->reg[SC89601_REG_CTL5];
    snapshot->input_det_done = (value & 0x40u) >> 6;

    return SC89601_OK;
}


/* 第6步不仅等待适配器，也是无外部输入时的OTG维护状态。 */
#define SC89601_STEP_WAIT_INPUT  6u
/* 第17步是充电配置完成后的轮询状态；充满后仍可停留在这里。 */
#define SC89601_STEP_RUNNING     17u

/* init：0表示配置/模式已确认，255表示等待或配置中；其他值为错误码，不是电量。 */
volatile sc_u8  g_sc89601_init_result = SC89601_PENDING;
/* poll：最近一次服务轮询的结果；0只代表该次检查成功，不代表永远没有故障。 */
volatile sc_u8  g_sc89601_poll_result = SC89601_PENDING;
/* 最近读回的数据缓存；没有重新读取的字段可能保留旧值。 */
volatile sc89601_snapshot_t g_sc89601_snapshot;

/* 当前配置步骤：0~5基础初始化、6选择模式、7~16充电配置、17充电轮询。 */
static sc_u8 s_sc89601_step = 0u;
/* 跳过接下来多少次100ms服务；4表示4次返回，第5次调用再处理，名义间隔500ms。 */
static sc_u8 s_sc89601_wait_ticks = 0u;
/* 最近一次模式探测是否确认没有外部输入，用于STOP资格判断。 */
static sc_u8 s_sc89601_input_absent = 0u;

/* 0:关闭；1:等待启动确认；2:升压状态已确认。 */
static sc_u8 s_sc89601_otg_state = 0u;
/* 应用层许可：只表达电池/业务是否允许OTG；有适配器输入时仍优先处理输入。 */
static sc_u8 s_sc89601_otg_allowed = 0u;

/*
 * 保存主程序提交的OTG许可，不在这里直接执行I2C或改变硬件输出。
 * 许可变化且处于第6步时取消软件等待，让下次100ms服务及时处理。
 */
void sc89601_set_otg_allowed(sc_u8 allowed)
{
    allowed = allowed ? 1u : 0u;

    if (s_sc89601_otg_allowed != allowed) {
        s_sc89601_otg_allowed = allowed;

        if (s_sc89601_step == SC89601_STEP_WAIT_INPUT) {
            s_sc89601_wait_ticks = 0u;
        }
    }
}

/*
 * 返回最近一次已确认的OTG状态；2映射为1，其余映射为0。
 * 这是软件确认缓存，不能当作实时电压测量值；外设也不由该接口直接开启。
 */
sc_u8 sc89601_otg_is_active(void)
{
    return (s_sc89601_otg_state == 2u) ? 1u : 0u;
}

/*
 * 将读回值按mask筛选后与expected比较；不匹配返回VERIFY(8)。
 * expected应只包含mask范围内的目标位，忽略位不会参与比较。
 */
static sc_u8 sc89601_expect(
    sc_u8 reg, sc_u8 mask, sc_u8 expected)
{
    sc_u8 result;
    sc_u8 value;

    result = sc89601_read_reg(reg, &value);
    if (result != SC89601_OK) {
        return result;
    }

    return ((value & mask) == expected) ?
        SC89601_OK : SC89601_ERR_VERIFY;
}

/*
 * 模式退出：先关闭充电和OTG并读回确认，再恢复HIZ/CTL4设置。
 * 成功后OTG软件状态清零；此函数不立即开启充电，后续识别及配置流程才开启。
 */
static sc_u8 sc89601_otg_leave(void)
{
    sc_u8 result;

    /* 0x30选择bit5(OTG_CFG)和bit4(CHG_CFG)，写0使两种模式都先关闭。 */
    result = sc89601_update_bits(
        SC89601_REG_CTL1, 0x30u, 0x00u);
    if (result != SC89601_OK) return result;

    result = sc89601_expect(
        SC89601_REG_CTL1, 0x30u, 0x00u);
    if (result != SC89601_OK) return result;

    /* 0x80只选择EN_HIZ；清为0，保留输入路径和内部供电所需条件。 */
    result = sc89601_update_bits(
        SC89601_REG_IINDPM, 0x80u, 0x00u);
    if (result != SC89601_OK) return result;

    /* 恢复充电流程使用的CTL4值；此时OTG已关闭，不以此值表示正在升压。 */
    result = sc89601_write_reg(
        SC89601_REG_CTL4, 0x24u);
    if (result != SC89601_OK) return result;

    s_sc89601_otg_state = 0u;
    g_sc89601_init_result = SC89601_PENDING;

    return SC89601_OK;
}

/*
 * 配置电池升压：先关模式、写参数并读回，再检查外部输入，最后使能OTG。
 * 返回OK可能表示已提交启动，也可能表示期间检测到输入而正常退出OTG。
 * 提交启动后软件状态为1；由下一轮power_probe确认硬件OTG状态后才变为2。
 */
static sc_u8 sc89601_otg_enter(void)
{
    sc_u8 result;
    sc_u8 value;

    /* 禁止充电和OTG同时开启。 */
    result = sc89601_update_bits(
        SC89601_REG_CTL1, 0x30u, 0x00u);
    if (result != SC89601_OK) return result;

    result = sc89601_expect(
        SC89601_REG_CTL1, 0x30u, 0x00u);
    if (result != SC89601_OK) return result;

    /*
     * R00=0x1B：EN_HIZ(bit7)=0，IINDPM[4:0]=27，对应2800mA。
     * 当前已验证方案保持HIZ关闭；0x9B会置HIZ，不能把它当作仅隔离VBUS的开关。
     */
    result = sc89601_write_reg(
        SC89601_REG_IINDPM, 0x1Bu);
    if (result != SC89601_OK) return result;

    /* 现有函数同时设置BOOST_LIM=1，目标R02为8A。 */
    result = sc89601_set_charge_current(
        SC89601_TEST_ICHG_MA);
    if (result != SC89601_OK) return result;

    /*
     * R06=FF：ACOVP[7:6]=11选14.2V；BOOSTV[2:1]对应的两位选400+200mV；
     * VINDPM[3:0]=1111选8.4V。BOOSTV其余两位在REG0D中配置。
     */
    result = sc89601_write_reg(
        SC89601_REG_VINDPM_BOOSTV_OVP, 0xFFu);
    if (result != SC89601_OK) return result;

    /*
     * R0D=22：BOOSTV[3](REG0D.bit2)=0，BOOSTV[0](bit1)=1。
     * 结合R06的400mV、200mV，目标为3.9V+0.4V+0.2V+0.1V=4.6V。
     */
    result = sc89601_write_reg(
        SC89601_REG_CTL4, 0x22u);
    if (result != SC89601_OK) return result;

    /* 写参数期间若插电，交回充电识别。 */
    result = sc89601_read_reg(
        SC89601_REG_STAT2, &value);
    if (result != SC89601_OK) return result;

    /*
     * 0x84选择REG0A的VBUS_GD(bit7)和ACOV_STAT(bit2)。
     * 发现输入或输入过压都停止本次启动；过压时等待输入恢复，不立即充电。
     */
    if ((value & 0x84u) != 0u) {
        s_sc89601_input_absent = 0u;
        return sc89601_otg_leave();
    }

    result = sc89601_expect(
        SC89601_REG_IINDPM, 0xFFu, 0x1Bu);
    if (result != SC89601_OK) return result;

    /* 这里只核对升压限流bit7为1；OTG电压及R00/R06/R0D另行核对。 */
    result = sc89601_expect(
        SC89601_REG_ICC, 0x80u, 0x80u);
    if (result != SC89601_OK) return result;

    result = sc89601_expect(
        SC89601_REG_VINDPM_BOOSTV_OVP, 0xFFu, 0xFFu);
    if (result != SC89601_OK) return result;

    result = sc89601_expect(
        SC89601_REG_CTL4, 0xFFu, 0x22u);
    if (result != SC89601_OK) return result;

    /*
     * 保存并消费上一轮故障锁存。
     * 本轮OTG启动后的故障仍由power_probe检查。
     */
    result = sc89601_read_reg(
        SC89601_REG_FAULT, &value);
    if (result != SC89601_OK) return result;
    g_sc89601_snapshot.fault_latched = value;

    result = sc89601_read_reg(
        SC89601_REG_FAULT, &value);
    if (result != SC89601_OK) return result;
    g_sc89601_snapshot.fault_current = value;
    g_sc89601_snapshot.reg[SC89601_REG_FAULT] = value;

    /* 配置期间可能插电，开启OTG前再检查一次。 */
    result = sc89601_read_reg(
        SC89601_REG_STAT2, &value);
    if (result != SC89601_OK) return result;

    if ((P50 == 0) || ((value & 0x84u) != 0u)) {
        s_sc89601_input_absent = 0u;
        return sc89601_otg_leave();
    }
    /*
     * OTG开启、充电关闭，并喂一次看门狗。
     * 0x68 = WD_RST(bit6) + OTG_CFG(bit5) + SYS_MIN[3:1]=100(3.4V)。
     * CHG_CFG(bit4)=0；WD_RST自清后R01读回0x28，喂狗位不是长期状态位。
     */
    result = sc89601_write_reg(
        SC89601_REG_CTL1, 0x68u);
    if (result != SC89601_OK) return result;

    s_sc89601_wdt_seconds = 0u;
    /* I2C写成功先记为等待确认，不能在此直接宣布升压已建立。 */
    s_sc89601_otg_state = 1u;
    g_sc89601_init_result = SC89601_PENDING;

    return SC89601_OK;
}

/*
 * 共用电源巡检：识别外部输入；无输入且OTG已提交时，再确认升压状态和故障。
 * 调用者传入有效input_present指针；返回结果与输入标志是两个不同信息。
 * 检测到外部输入优先交给调用者退出OTG，避免把正常的模式退出误判为升压故障。
 */
static sc_u8 sc89601_power_probe(sc_u8 *input_present)
{
    sc_u8 result;
    sc_u8 reg00;
    sc_u8 reg01;
    sc_u8 reg08;
    sc_u8 reg0a;
    sc_u8 fault;

    result = sc89601_read_reg(
        SC89601_REG_STAT2, &reg0a);
    if (result != SC89601_OK) return result;

    result = sc89601_read_reg(
        SC89601_REG_STAT1, &reg08);
    if (result != SC89601_OK) return result;

    g_sc89601_snapshot.reg[SC89601_REG_STAT1] = reg08;
    g_sc89601_snapshot.reg[SC89601_REG_STAT2] = reg0a;
    g_sc89601_snapshot.vbus_status = (reg08 >> 5) & 7u;
    g_sc89601_snapshot.charge_status = (reg08 >> 3) & 3u;
    g_sc89601_snapshot.power_good = (reg08 >> 2) & 1u;
    g_sc89601_snapshot.vbus_good = (reg0a >> 7) & 1u;

    /*
     * P50低或PG有效：已经确认外部输入。
     * 输入过压时也禁止OTG，但继续等待输入恢复正常。
     * REG08.bit2为PG；REG0A.bit2为输入过压，bit7为VBUS_GD。
     * REG08[7:5]=111(0xE0)表示OTG；排除这种状态，避免把自己的升压当成适配器。
     */
    *input_present =
        ((P50 == 0) ||
         ((reg08 & 0x04u) != 0u) ||
         ((reg0a & 0x04u) != 0u) ||
         (((reg0a & 0x80u) != 0u) &&
          ((reg08 & 0xE0u) != 0xE0u))) ? 1u : 0u;

    /* 软件尚未要求OTG时，只报告输入情况，不用OTG条件检查当前硬件。 */
    if (s_sc89601_otg_state == 0u) {
        return SC89601_OK;
    }

    result = sc89601_read_reg(
        SC89601_REG_IINDPM, &reg00);
    if (result != SC89601_OK) return result;

    g_sc89601_snapshot.reg[SC89601_REG_IINDPM] = reg00;

    /*
     * 当前R00的HIZ固定为0，不能通过该位变化判断插入电源。
     * 外部输入优先：允许调用者先关OTG，再等待输入识别。
     */
    if (*input_present) {
        return SC89601_OK;
    }

    result = sc89601_read_reg(
        SC89601_REG_CTL1, &reg01);
    if (result != SC89601_OK) return result;

    g_sc89601_snapshot.reg[SC89601_REG_CTL1] = reg01;

    /*
     * 只检查模式位：要求OTG_CFG=1且CHG_CFG=0。
     * 若读到R01=08，模式位已关闭；此处记录的是异常处理前的现场。
     */
    if ((reg01 & 0x30u) != 0x20u) {
        /* 此处尚未执行异常退出，记录启动确认失败现场。 */
        printf(
            "\r\n[OTG-CHECK] R00=%02X R01=%02X R08=%02X R0A=%02X\r\n",
            (unsigned int)reg00,
            (unsigned int)reg01,
            (unsigned int)reg08,
            (unsigned int)reg0a);

        return SC89601_ERR_VERIFY;
    }

    /* 连读两次，分别保留锁存故障和当前故障。 */
    result = sc89601_read_reg(
        SC89601_REG_FAULT, &fault);
    if (result != SC89601_OK) return result;

    g_sc89601_snapshot.fault_latched = fault;

    result = sc89601_read_reg(
        SC89601_REG_FAULT, &fault);
    if (result != SC89601_OK) return result;

    g_sc89601_snapshot.fault_current = fault;
    g_sc89601_snapshot.reg[SC89601_REG_FAULT] = fault;

    /*
     * 0xC8=看门狗故障(bit7)+升压故障(bit6)+电池过压(bit3)。
     * 当前故障命中或第一读曾锁存升压故障，都判定本轮OTG检查失败。
     * BOOST_FAULT是综合故障标志，不能仅凭它确定是过流、欠压还是其他硬件原因。
     */
    if ((fault & 0xC8u) ||
        (g_sc89601_snapshot.fault_latched & 0x40u)) {
        return SC89601_ERR_VERIFY;
    }

    /* 除了OTG使能位，还要求VBUS_STAT=7，确认芯片报告已处于OTG模式。 */
    if ((reg08 & 0xE0u) != 0xE0u) {
        return SC89601_ERR_VERIFY;
    }

    /* 模式和故障检查通过，才标记为OTG已确认。 */
    s_sc89601_otg_state = 2u;
    return SC89601_OK;
}

/*
 * 统一异常处理：保留失败步骤、OTG状态和故障现场，尝试关模式，随后延时重试。
 * 约5秒后从第0步重新初始化；保留应用层OTG许可，下一轮重新判断电源。
 * 关闭命令是尽力而为：I2C本身失效时，不能保证芯片收到写入。
 * 日志标签沿用OTG-FAIL；该函数也可能用于充电配置或通信失败。
 */
static void sc89601_service_failed(sc_u8 result)
{
    /* 最后两个地址都是REG09，分别采集故障锁存与当前值。 */
    static const sc_u8 code reg_list[8] = {
        0x00u, 0x01u, 0x05u, 0x08u,
        0x0Au, 0x0Du, 0x09u, 0x09u
    };

    sc_u8 values[8];
    sc_u8 count;
    sc_u8 index;
    sc_u8 read_result;
    sc_u8 failed_step;
    sc_u8 failed_otg_state;
    sc_u8 previous_latched;
    sc_u8 previous_current;

    failed_step = s_sc89601_step;
    failed_otg_state = s_sc89601_otg_state;

    /*
     * 前面的巡检可能已经读取过R09。
     * 同时保留此前缓存，避免遗漏已被读出的锁存故障。
     */
    previous_latched = g_sc89601_snapshot.fault_latched;
    previous_current = g_sc89601_snapshot.fault_current;

    read_result = SC89601_OK;

    for (count = 0u; count < 8u; count++) {
        read_result = sc89601_read_reg(
            reg_list[count], &values[count]);

        if (read_result != SC89601_OK) {
            break;
        }
    }

    /* 保留原有异常退出及重试逻辑。 */
    (void)sc89601_update_bits(
        SC89601_REG_CTL1, 0x30u, 0x00u);

    s_sc89601_otg_state = 0u;
    g_sc89601_init_result = result;
    g_sc89601_poll_result = result;

    s_sc89601_step = 0u;
    /* 先跳过49次100ms服务，第50次再执行第0步；名义重试间隔约5秒。 */
    s_sc89601_wait_ticks = 49u;
    s_sc89601_input_absent = 0u;

    /*
     * 暂停软件周期喂狗，后续初始化成功配置芯片看门狗后再恢复。
     * 这里只清软件许可，没有向芯片写入关闭看门狗的配置。
     */
    s_sc89601_wdt_enabled = 0u;
    s_sc89601_wdt_seconds = 0u;

    printf(
        "\r\n[OTG-FAIL] err=%u step=%u os=%u prevF=%02X/%02X",
        (unsigned int)result,
        (unsigned int)failed_step,
        (unsigned int)failed_otg_state,
        (unsigned int)previous_latched,
        (unsigned int)previous_current);

    for (index = 0u; index < count; index++) {
        printf(
            " R%02X=%02X",
            (unsigned int)reg_list[index],
            (unsigned int)values[index]);
    }

    if (read_result != SC89601_OK) {
        printf(
            " READ_FAIL_R%02X=%u",
            (unsigned int)reg_list[count],
            (unsigned int)read_result);
    }

    printf("\r\n");
}

/*
 * BTM秒任务维护入口：每10秒检查R05并向REG01.WD_RST写1。
 * MCU STOP时也必须由主循环巡检路径调用，避免升压期间芯片看门狗超时。
 * 只核对R05这一项维护配置；它不等于每10秒完整校验全部寄存器。
 * SC89601看门狗超时会使相关寄存器回到默认配置，随后需要软件重新配置；
 * 它不负责复位MCU或Wi-Fi，也不能保证所有类型的硬件异常都能自动恢复。
 */
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
        /* 只置bit6发出喂狗命令，保留当前充电/OTG模式；该命令位自清。 */
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

/*
 * 上电软件初始化，不等同于已经完成芯片寄存器配置。
 * 仅初始化GPIO、服务状态和缓存；实际寄存器配置在后续100ms任务分步执行。
 * STOP返回后不重调，否则会清掉OTG许可、确认状态和喂狗维护许可。
 */
void sc89601_service_init(void)
{
    sc_u8 reg;

    sc89601_gpio_init();

    s_sc89601_otg_state = 0u;
    s_sc89601_otg_allowed = 0u;
  
    s_sc89601_step = 0u;
    /* 第一次100ms调用仅消耗等待计数；下一次再开始第0步总线恢复。 */
    s_sc89601_wait_ticks = 1u;
    s_sc89601_input_absent = 0u;
    s_sc89601_wdt_enabled = 0u;
    s_sc89601_wdt_seconds = 0u;
  
    g_sc89601_init_result = SC89601_PENDING;
    g_sc89601_poll_result = SC89601_PENDING;

    /* 这里只清RAM缓存为0；不是把这些芯片寄存器写成0。 */
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
 * 正常主循环的100ms服务，按步骤配置，避免长时间循环等待输入识别。
 * 每次I2C事务仍是同步执行；分步调度让按键、UART和MCU喂狗能获得处理机会。
 * switch内break：成功后走函数末尾step++；return：保持当前步骤等待/巡检。
 * 第6步维护电池OTG；第17步维护外部供电下的充电，二者都用return保持状态。
 */
void sc89601_service_100ms(void)
{
    sc_u8 result;
    sc_u8 device_reg;
    sc_u8 input_present;

    /* 软件等待期间不执行本函数的寄存器步骤；BTM秒任务喂狗仍独立运行。 */
    if (s_sc89601_wait_ticks != 0u) {
        s_sc89601_wait_ticks--;
        return;
    }

    result = SC89601_OK;

    switch (s_sc89601_step) {
        /* 步骤0：标记配置进行中，恢复I2C总线。 */
        case 0u:
            g_sc89601_init_result = SC89601_PENDING;
            g_sc89601_poll_result = SC89601_PENDING;
            result = sc89601_bus_recover();
            break;

        /* 步骤1：核对芯片PN=3，并保存型号/版本供日志查看。 */
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

        /* 步骤2：暂停软件喂狗维护并复位芯片寄存器，后面重新建立全部配置。 */
        case 2u:
            s_sc89601_wdt_enabled = 0u;
            s_sc89601_wdt_seconds = 0u;

            result = sc89601_reset_registers();
            break;

        case 3u:
            /*
             * 步骤3：R01=08，SYS_MIN=3.4V，充电和OTG都关闭。
             * SYS_MIN属于VSYS最低电压参数，不表示PMID被限制在3.4V。
             */
            result = sc89601_write_reg(
                SC89601_REG_CTL1, 0x08u);
            break;

        /* 步骤4：清EN_HIZ(bit7)，其余REG00位保留。 */
        case 4u:
            result = sc89601_update_bits(
                SC89601_REG_IINDPM, 0x80u, 0x00u);
            break;

        case 5u:
            /*
             * 步骤5：先把R06设为FF，建立14.2V过压阈值和8.4V VINDPM。
             * 不能带着原6.4V过压阈值等待12V输入识别，否则输入可能一直不被接受。
             */
            result = sc89601_write_reg(
                SC89601_REG_VINDPM_BOOSTV_OVP, 0xFFu);
            if (result != SC89601_OK) {
                break;
            }

            /*
             * R05按0xBF掩码写9D，保留bit6；启用终止、40秒看门狗和10小时安全计时。
             * 同时沿用TREG=90摄氏度及低温充电限流配置；本次不修改已验证参数。
             */
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

        /*
         * 步骤6：共用模式判断；外部输入优先，无输入时按应用层许可维护OTG。
         * power_probe也在OTG已提交后执行硬件确认，所以先检查结果再继续决策。
         */
        case SC89601_STEP_WAIT_INPUT:
            result = sc89601_power_probe(&input_present);
            g_sc89601_poll_result = result;

            if (result != SC89601_OK) {
                break;
            }

            s_sc89601_input_absent =
                input_present ? 0u : 1u;

            /* 检测到外部输入或输入过压：禁止继续电池OTG；后面再判断能否正常充电。 */
            if (input_present) {
                if (s_sc89601_otg_state != 0u) {
                    result = sc89601_otg_leave();
                    if (result != SC89601_OK) {
                        break;
                    }

                    /* 退出OTG后跳过下一次服务，再重新读取输入状态，避免沿用切换前的状态。 */
                    s_sc89601_wait_ticks = 1u;
                    return;
                }

                /*
                 * 充电前要求VBUS_GD=1、PG=1、输入类型非0且不是OTG类型7。
                 * 条件未满足时留在第6步，每名义500ms再查，不反复复位芯片。
                 */
                if (!g_sc89601_snapshot.vbus_good ||
                    !g_sc89601_snapshot.power_good ||
                    !g_sc89601_snapshot.vbus_status ||
                    (g_sc89601_snapshot.vbus_status == 7u)) {

                    s_sc89601_wait_ticks = 4u;
                    return;
                }

                /* 输入识别完成，进入第7步原充电配置。 */
                /* 这里break后会走step++，转入第7步；尚未开启充电，init保持等待。 */
                g_sc89601_init_result = SC89601_PENDING;
                break;
            }

            /*
             * 没有外部输入时才处理OTG许可；低电量等业务原因取消许可时先退出OTG。
             * 本驱动不读取工作状态STATE_OFF，也不直接写水泵控制脚。
             */
            if (!s_sc89601_otg_allowed) {
                if (s_sc89601_otg_state != 0u) {
                    result = sc89601_otg_leave();
                    if (result != SC89601_OK) {
                        break;
                    }
                }

                g_sc89601_init_result = SC89601_PENDING;
            } else if (s_sc89601_otg_state == 0u) {
                /* 业务允许且尚未启动：配置4.6V并提交启动；本轮仍待下一次硬件确认。 */
                result = sc89601_otg_enter();
                if (result != SC89601_OK) {
                    break;
                }
            } else {
                /* 本轮power_probe已确认OTG状态，配置结果才可记为OK。 */
                g_sc89601_init_result = SC89601_OK;
            }

            /* 下一轮确认升压状态，不阻塞主循环等待。 */
            s_sc89601_wait_ticks = 4u;
            return;

        /* 步骤7：配置VINDPM=8.4V，只修改R06低4位。 */
        case 7u:
            result = sc89601_set_input_voltage_limit(
                SC89601_TEST_VINDPM_MV);
            break;

        /* 步骤8：配置输入过压阈值14.2V，只修改R06高2位。 */
        case 8u:
            result = sc89601_set_acovp_threshold(
                SC89601_TEST_ACOVP_MV);
            break;

        /*
         * 步骤9：输入识别后重新设2800mA输入限流。
         * 输入检测可能更新IINDPM，所以目标限流在识别完成后写入。
         */
        case 9u:
            result = sc89601_set_input_current_limit(
                SC89601_TEST_IINDPM_MA);
            break;

        /* 步骤10：当前VINDPM_TRACK目标为0，关闭电池电压跟踪。 */
        case 10u:
            result = sc89601_set_vindpm_track(
                SC89601_TEST_VINDPM_TRACK_MV);
            break;

        /* 步骤11：预充300mA，REG03高半字节为4。 */
        case 11u:
            result = sc89601_set_precharge_current(
                SC89601_TEST_IPRECHG_MA);
            break;

        /* 步骤12：终止180mA，REG03低半字节为2，合成R03=42。 */
        case 12u:
            result = sc89601_set_termination_current(
                SC89601_TEST_ITERM_MA);
            break;

        /* 步骤13：电池充电电压4.2V；随后设置REG04低3位，合成R04=5E。 */
        case 13u:
            result = sc89601_set_charge_voltage(
                SC89601_TEST_VREG_MV);

            if (result == SC89601_OK) {
                /*
                 * mask=07选择TOP_OFF_TIMER[2:1]和VRECHG(bit0)；data=06选45分钟、100mV。
                 * Top-off是满足终止条件后的补充充电时间，不是整个恒压阶段强制45分钟关断。
                 */
                result = sc89601_update_bits(
                    SC89601_REG_VBAT_REG, 0x07u, 0x06u);
            }
            break;

        /* 步骤14：设充电600mA，同时保留目标升压限流1.2A，R02=8A。 */
        case 14u:
            result = sc89601_set_charge_current(
                SC89601_TEST_ICHG_MA);
            break;

        /*
         * 步骤15：参数已配置，最后置CHG_CFG=1；OTG已在前面关闭。
         * 实际能否进入充电阶段还受输入、/CE及芯片保护条件约束。
         */
        case 15u:
            result = sc89601_set_charge_enable(1u);
            break;

        /* 步骤16：读回目标表；通过后才把init置为OK并进入第17步。 */
        case 16u:
            result = sc89601_verify_minimal_config();
            break;

        /*
         * 步骤17：外部输入下的充电轮询，通常每名义1秒读取全寄存器缓存。
         * 芯片自行处理预充/恒流/恒压/充满；充满不会使软件退出本步骤。
         */
        case SC89601_STEP_RUNNING:
            result = sc89601_read_snapshot(
                &g_sc89601_snapshot);

            g_sc89601_poll_result = result;

            if (result != SC89601_OK) {
                break;
            }
            /*
             * 当前WD故障(bit7)或R05配置不匹配时，尝试关模式并走约5秒重试。
             * 此分支没有把每一种充电故障都强制复位；其他保护还由芯片硬件执行。
             */
            if ((g_sc89601_snapshot.fault_current & 0x80u) ||
                ((g_sc89601_snapshot.reg[SC89601_REG_CTL2] &
                  0xBFu) != 0x9Du)) {

                (void)sc89601_update_bits(
                    SC89601_REG_CTL1, 0x30u, 0x00u);

                sc89601_service_failed(SC89601_ERR_VERIFY);
                return;
            }
            if (g_sc89601_snapshot.vbus_good == 0u) {
                /*
                 * 拔电后等待新的输入识别。
                 * 再次插电时重新应用参数，不在此复位芯片。
                 */
                s_sc89601_input_absent = 1u;
                /* 检测到输入消失，转回第6步；后续再根据电量许可开启电池OTG。 */
                s_sc89601_step = SC89601_STEP_WAIT_INPUT;
                s_sc89601_wait_ticks = 4u;
                g_sc89601_init_result = SC89601_PENDING;
            } else {
                s_sc89601_input_absent = 0u;
                /* 跳过9次100ms服务，第10次再次轮询，名义间隔1秒。 */
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

    /*
     * 只有本步骤成功并通过break到达这里才推进步骤。
     * 第6/17步的等待、稳态分支使用return，不会误推进到其他步骤。
     */
    s_sc89601_step++;

    if (s_sc89601_step == SC89601_STEP_RUNNING) {
        g_sc89601_init_result = SC89601_OK;
    }
}

/*
 * 充电驱动给出的STOP资格：必须处于第6步且最近确认没有外部输入。
 * OTG允许时要求升压已确认；不允许时要求OTG已退出，切换中不能睡。
 * 它只检查本驱动条件；主程序仍需检查按键、报警显示、泵/加热/UVC等业务。
 */
sc_u8 sc89601_can_stop(void)
{
    if ((s_sc89601_step != SC89601_STEP_WAIT_INPUT) ||
        !s_sc89601_input_absent) {
        return 0u;
    }

    if (s_sc89601_otg_allowed) {
        /* 确认升压已建立，才允许进入STOP。 */
        return (s_sc89601_otg_state == 2u) ? 1u : 0u;
    }

    return (s_sc89601_otg_state == 0u) ? 1u : 0u;
}

/*
 * STOP期间的主循环巡检接口：返回1表示应恢复正常调度，0表示可继续睡。
 * 复用power_probe检查外部输入、OTG模式及故障，不重复上电初始化。
 * 发现输入先退出OTG，再由正常100ms任务等待识别并重新配置充电。
 * 无输入且当前模式符合STOP条件时保持原模式；本函数不关闭正常OTG输出。
 */
sc_u8 sc89601_sleep_wake_required(void)
{
    sc_u8 result;
    sc_u8 input_present;

    /* 原有睡眠资格失效，直接要求应用层退出STOP，不继续按睡眠稳态运行。 */
    if (!sc89601_can_stop()) {
        return 1u;
    }

    result = sc89601_power_probe(&input_present);
    g_sc89601_poll_result = result;

    if (result != SC89601_OK) {
        sc89601_service_failed(result);
        return 1u;
    }

    /* 插入输入时取消第6步的等待，让恢复后的100ms任务及时处理充电流程。 */
    if (input_present) {
        s_sc89601_input_absent = 0u;
        s_sc89601_wait_ticks = 0u;

        if (s_sc89601_otg_state != 0u) {
            result = sc89601_otg_leave();

            if (result != SC89601_OK) {
                sc89601_service_failed(result);
            }
        }

        return 1u;
    }

    /* 无外部输入、升压正常：保持OTG并继续休眠。 */
    return 0u;
}
