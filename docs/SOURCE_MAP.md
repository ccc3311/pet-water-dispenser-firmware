# 文件来源与路径对应

最新来源：2026-10-21 从桌面 `TL2_T12` 读取；以下文件均保留原始字节。完整工程位于 `TL2_T12/`，以 `Keil_C/Project/TL2_6.uvproj` 为构建入口。

原仓库的 24 份源码阅读路径同步为对应桌面文件，映射如下：

| 既有阅读路径 | 桌面工程对应路径 |
| --- | --- |
| `User/main.c` | `TL2_T12/Keil_C/User/main.c` |
| `User/SC_Init.c` | `TL2_T12/Keil_C/User/SC_Init.c` |
| `App/app_fsm.c` | `TL2_T12/Keil_C/User/src/app_fsm.c` |
| `HAL/hal_adc.c` | `TL2_T12/Keil_C/Hardware/hal_adc.c` |
| `TuyaMCUSDK/mcu_api.c` | `TL2_T12/Keil_C/Wifi/mcu_sdk/mcu_api.c` |
| `TuyaMCUSDK/mcu_api.h` | `TL2_T12/Keil_C/Wifi/mcu_sdk/mcu_api.h` |
| `TuyaMCUSDK/protocol.c` | `TL2_T12/Keil_C/Wifi/mcu_sdk/protocol.c` |
| `TuyaMCUSDK/protocol.h` | `TL2_T12/Keil_C/Wifi/mcu_sdk/protocol.h` |
| `TuyaMCUSDK/system.c` | `TL2_T12/Keil_C/Wifi/mcu_sdk/system.c` |
| `Drivers/SC89601/sc89601.c` | `TL2_T12/Keil_C/PWR/sc89601.c` |
| `App/app_wifi.c` | `TL2_T12/Keil_C/PWR/app_wifi.c` |
| `TuyaMCUSDK/system.h` | `TL2_T12/Keil_C/Wifi/mcu_sdk/system.h` |
| `TuyaMCUSDK/wifi.h` | `TL2_T12/Keil_C/Wifi/mcu_sdk/wifi.h` |
| `Drivers/SC89601/sc89601.h` | `TL2_T12/Keil_C/PWR/sc89601.h` |
| `HAL/hal_power.c` | `TL2_T12/Keil_C/PWR/hal_power.c` |
| `HAL/hal_lpd.c` | `TL2_T12/Keil_C/Hardware/hal_lpd.c` |
| `HAL/hal_eeprom.c` | `TL2_T12/Keil_C/Hardware/hal_eeprom.c` |
| `HAL/hal_uart.c` | `TL2_T12/Keil_C/Hardware/hal_uart.c` |
| `HAL/hal_touch.c` | `TL2_T12/Keil_C/Hardware/hal_touch.c` |
| `HAL/hal_tm1650.c` | `TL2_T12/Keil_C/Hardware/hal_tm1650.c` |
| `User/SC_it.c` | `TL2_T12/Keil_C/User/SC_it.c` |
| `HAL/hal_buzzer.c` | `TL2_T12/Keil_C/Hardware/hal_buzzer.c` |
| `HAL/hal_ir.c` | `TL2_T12/Keil_C/Hardware/hal_ir.c` |
| `HAL/hal_pwm.c` | `TL2_T12/Keil_C/Hardware/hal_pwm.c` |

`HAL/hal_power.c` 以 `.uvproj` 实际选择的 `PWR/hal_power.c` 为来源；原始 `Hardware/hal_power.c` 仍保存在完整工程中。`Project/1.c` 为目录内未被当前目标引用的源文件，也按原样归档。

原始附件及对话转录仍位于 `archive/`。上一版来源清单另存为 `archive/provenance/2026-10-09_source_manifest.json`，其路径与哈希对应旧提交 `4a3db19e16f1e9a9bd295a47ec4274f1f90baddb`。

`docs/source_manifest.json` 记录本次 395 份工程文件和 24 份阅读镜像的来源、Git blob SHA 与 SHA-256；`SHA256SUMS.txt` 覆盖最终仓库除自身以外的文件。
