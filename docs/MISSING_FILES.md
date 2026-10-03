# 尚未提供的工程依赖

本清单只根据当前文件引用及工程格式整理；不是为缺失文件生成替代实现。

## 直接引用但未提供

| 引用 | 引用文件 |
| --- | --- |
| `../Drivers/SCDriver_list.h` | `User/SC_Init.c`, `User/SC_it.c`, `User/main.c` |
| `HeadFiles/SC_itExtern.h` | `User/SC_Init.c`, `User/SC_it.c` |
| `HeadFiles/SysFunVarDefine.h` | `HAL/hal_eeprom.c`, `HAL/hal_touch.c`, `User/main.c` |
| `SC_Init.h` | `HAL/hal_uart.c`, `User/SC_Init.c`, `User/main.c`, `Drivers/SC89601/sc89601.c` |
| `SC_it.h` | `User/SC_it.c`, `User/main.c` |
| `app_fsm.h` | `HAL/hal_eeprom.c`, `HAL/hal_touch.c`, `HAL/hal_buzzer.c`, `HAL/hal_adc.c`, `HAL/hal_ir.c`, `TuyaMCUSDK/protocol.c`, `App/app_wifi.c`, `App/app_fsm.c`, `User/main.c` |
| `app_wifi.h` | `TuyaMCUSDK/protocol.c`, `App/app_wifi.c`, `App/app_fsm.c`, `User/main.c` |
| `hal_adc.h` | `HAL/hal_adc.c`, `TuyaMCUSDK/protocol.c`, `App/app_wifi.c`, `App/app_fsm.c`, `User/main.c` |
| `hal_buzzer.h` | `HAL/hal_touch.c`, `HAL/hal_buzzer.c`, `HAL/hal_adc.c`, `TuyaMCUSDK/protocol.c`, `App/app_fsm.c`, `User/SC_it.c`, `User/main.c` |
| `hal_eeprom.h` | `HAL/hal_lpd.c`, `HAL/hal_eeprom.c`, `TuyaMCUSDK/protocol.c`, `App/app_fsm.c`, `User/SC_it.c` |
| `hal_ir.h` | `HAL/hal_ir.c`, `App/app_fsm.c`, `User/main.c` |
| `hal_lpd.h` | `HAL/hal_lpd.c`, `User/main.c` |
| `hal_power.h` | `HAL/hal_power.c`, `App/app_fsm.c`, `User/SC_it.c`, `User/main.c` |
| `hal_pwm.h` | `HAL/hal_pwm.c`, `TuyaMCUSDK/protocol.c`, `App/app_wifi.c`, `App/app_fsm.c`, `User/main.c` |
| `hal_tm1650.h` | `HAL/hal_tm1650.c`, `App/app_fsm.c`, `User/main.c` |
| `hal_touch.h` | `HAL/hal_touch.c`, `App/app_fsm.c`, `User/SC_it.c`, `User/main.c` |
| `hal_uart.h` | `HAL/hal_uart.c`, `TuyaMCUSDK/protocol.c`, `TuyaMCUSDK/protocol.c`, `App/app_wifi.c`, `User/main.c` |
| `sc92f_lpd.h` | `HAL/hal_lpd.c` |
| `tuya_type.h` | `TuyaMCUSDK/system.h`, `TuyaMCUSDK/wifi.h`, `TuyaMCUSDK/protocol.h`, `TuyaMCUSDK/mcu_api.h`, `User/main.c` |

## 条件编译的测试模板

以下 `.code_snippet` 出现在 `CONFIG_TUYA_ENABLE_TEST` 条件分支中。是否需要由最终 Keil 宏配置决定，不能一概当作正常产品构建必需文件。

- `dp_define.code_snippet`
- `dp_handle.code_snippet`
- `dp_id.code_snippet`
- `dp_id_handle.code_snippet`
- `dp_var.code_snippet`

## 未提供的构建资料

- 原始 Keil `.uvproj/.uvprojx` 工程文件、芯片宏、Include Paths、编译/链接设置及目标资源配置。
- 与当前 MCU 匹配的完整赛元驱动及所需器件/寄存器定义。
- `HeadFiles` 对应的声明和实现，例如初始化入口及相关全局变量定义。
- 最终硬件原理图和与本快照对应的完整 Rebuild 日志（用于核对配置，不影响源码归档）。

当前快照应先用于阅读和备份，补齐依赖后再验证编译。未编造这些文件，也未引入桌面类型替代 C51 寄存器类型。
