#ifndef __SC89601_H__
#define __SC89601_H__

/* Keil C51：unsigned char 为 8 位，unsigned int 为 16 位。 */
typedef unsigned char  sc_u8;
typedef unsigned int   sc_u16;

/*
 * SC89601 数据手册给出的 7 位地址是 0x6B。
 * 软件 I2C 内部会自动拼出写地址 0xD6、读地址 0xD7。
 */
#define SC89601_I2C_ADDR_7BIT          0x6Bu

/* 最小验证参数；首次上板使用低电流，确认波形和温升后再提高。 */
#define SC89601_TEST_IINDPM_MA         2800u
#define SC89601_TEST_VINDPM_MV         8400u
#define SC89601_TEST_ICHG_MA           600u
#define SC89601_TEST_VREG_MV           4200u
#define SC89601_TEST_IPRECHG_MA        300u
#define SC89601_TEST_ITERM_MA          180u
#define SC89601_TEST_ACOVP_MV          14200u
#define SC89601_TEST_VINDPM_TRACK_MV   0u

/* SC89601 寄存器地址。 */
#define SC89601_REG_IINDPM              0x00u
#define SC89601_REG_CTL1                0x01u
#define SC89601_REG_ICC                 0x02u
#define SC89601_REG_ITC_ITERM           0x03u
#define SC89601_REG_VBAT_REG            0x04u
#define SC89601_REG_CTL2                0x05u
#define SC89601_REG_VINDPM_BOOSTV_OVP   0x06u
#define SC89601_REG_CTL3                0x07u
#define SC89601_REG_STAT1               0x08u
#define SC89601_REG_FAULT               0x09u
#define SC89601_REG_STAT2               0x0Au
#define SC89601_REG_DEVICE              0x0Bu
#define SC89601_REG_JEITA               0x0Cu
#define SC89601_REG_CTL4                0x0Du
#define SC89601_REG_CTL5                0x0Eu
#define SC89601_REG_COUNT               0x0Fu

/* 驱动返回值。全部为非负值，便于 Keil Watch 直接观察。 */
typedef enum
{
    SC89601_OK = 0,
    SC89601_ERR_BUS_BUSY,
    SC89601_ERR_SCL_TIMEOUT,
    SC89601_ERR_ADDR_NACK,
    SC89601_ERR_REG_NACK,
    SC89601_ERR_DATA_NACK,
    SC89601_ERR_INPUT_DET_TIMEOUT,
    SC89601_ERR_DEVICE_ID,
    SC89601_ERR_VERIFY,
    SC89601_ERR_NULL
} sc89601_result_t;

/*
 * 无 UART/LED 时供 Keil Watch 观察。
 * reg[0x00...0x0E] 是当前寄存器快照；REG09 按手册连续读取两次，
 * fault_latched 保存第一次既有锁存，fault_current 保存第二次当前故障。
 */
typedef struct
{
    sc_u8 reg[SC89601_REG_COUNT];
    sc_u8 fault_latched;
    sc_u8 fault_current;
    sc_u8 part_number;
    sc_u8 revision;
    sc_u8 vbus_status;
    sc_u8 charge_status;
    sc_u8 power_good;
    sc_u8 vbus_good;
    sc_u8 input_det_done;
} sc89601_snapshot_t;

void sc89601_gpio_init(void);
void sc89601_delay_ms(sc_u16 ms);
sc_u8 sc89601_bus_recover(void);

sc_u8 sc89601_read_reg(sc_u8 reg, sc_u8 *data_value);
sc_u8 sc89601_write_reg(sc_u8 reg, sc_u8 data_value);
sc_u8 sc89601_update_bits(sc_u8 reg, sc_u8 mask, sc_u8 data_value);

sc_u8 sc89601_check_device(sc_u8 *device_reg);
sc_u8 sc89601_reset_registers(void);
sc_u8 sc89601_set_input_current_limit(sc_u16 current_ma);
sc_u8 sc89601_set_input_voltage_limit(sc_u16 voltage_mv);
sc_u8 sc89601_set_charge_current(sc_u16 current_ma);
sc_u8 sc89601_set_charge_voltage(sc_u16 voltage_mv);
sc_u8 sc89601_set_precharge_current(sc_u16 current_ma);
sc_u8 sc89601_set_termination_current(sc_u16 current_ma);
sc_u8 sc89601_set_acovp_threshold(sc_u16 voltage_mv);
sc_u8 sc89601_set_vindpm_track(sc_u16 offset_mv);
sc_u8 sc89601_set_charge_enable(sc_u8 enable);

sc_u8 sc89601_read_snapshot(volatile sc89601_snapshot_t *snapshot);


/* 初始化进行中或等待外部输入，不属于故障。 */
#define SC89601_PENDING  0xFFu

extern volatile sc_u8  g_sc89601_init_result;
extern volatile sc_u8  g_sc89601_poll_result;
extern volatile sc89601_snapshot_t g_sc89601_snapshot;

void sc89601_service_init(void);
void sc89601_service_100ms(void);
sc_u8 sc89601_can_stop(void);
sc_u8 sc89601_sleep_wake_required(void);

/* 由主循环中的BTM秒任务调用。 */
void sc89601_tick_1s(void);

/* 主循环更新升压许可；0禁止，1允许。 */
void sc89601_set_otg_allowed(sc_u8 allowed);

/* 返回1：已通过寄存器确认处于OTG模式。 */
sc_u8 sc89601_otg_is_active(void);

#endif
