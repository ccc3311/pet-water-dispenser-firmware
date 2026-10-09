# 文件来源

初始归档的原始附件位于 `archive/original_uploads/`，2026-09-30 对话转录副本位于 `archive/chat_transcription/`。原归档保留不变。2026-10-09 的三份工作源码以用户本次粘贴文本为当前来源；历史来源及当前 SHA-256 分别记录在 `source_manifest.json` 中。

| 整理路径 | 来源 | 处理方式 |
| --- | --- | --- |
| `App/app_fsm.c` | 粘贴的文本 (3).txt | 原字节复制 |
| `App/app_wifi.c` | 粘贴的文本 (11).txt | 原字节复制 |
| `Drivers/SC89601/sc89601.c` | 粘贴的文本 (10).txt | 原字节复制 |
| `Drivers/SC89601/sc89601.h` | sc89601.h | 原字节复制 |
| `HAL/hal_adc.c` | 粘贴的文本 (4).txt | 原字节复制 |
| `HAL/hal_buzzer.c` | 2026-09-30 user code message | 对话转录、空白排版 |
| `HAL/hal_eeprom.c` | 粘贴的 Markdown (1).md | Markdown 还原与拆分 |
| `HAL/hal_ir.c` | 2026-09-30 user code message | 对话转录、空白排版 |
| `HAL/hal_lpd.c` | 粘贴的 Markdown (1).md | Markdown 还原与拆分 |
| `HAL/hal_power.c` | 2026-10-09 user code message | 对话文本同步；有效代码与原基线一致，保留本次注释空白 |
| `HAL/hal_pwm.c` | 2026-09-30 user code message | 对话转录、空白排版 |
| `HAL/hal_tm1650.c` | 粘贴的 Markdown (1).md | Markdown 还原与拆分 |
| `HAL/hal_touch.c` | 粘贴的 Markdown (1).md | Markdown 还原与拆分 |
| `HAL/hal_uart.c` | 粘贴的 Markdown (1).md | Markdown 还原与拆分 |
| `TuyaMCUSDK/mcu_api.c` | 粘贴的文本 (5).txt | 原字节复制 |
| `TuyaMCUSDK/mcu_api.h` | 粘贴的文本 (6).txt | 原字节复制 |
| `TuyaMCUSDK/protocol.c` | 粘贴的文本 (7).txt | 原字节复制 |
| `TuyaMCUSDK/protocol.h` | 粘贴的文本 (8).txt | 原字节复制 |
| `TuyaMCUSDK/system.c` | 粘贴的文本 (9).txt | 原字节复制 |
| `TuyaMCUSDK/system.h` | 粘贴的文本 (1)(1).txt | 原字节复制 |
| `TuyaMCUSDK/wifi.h` | wifi.h | 原字节复制 |
| `User/SC_Init.c` | 粘贴的文本 (2).txt | 原字节复制 |
| `User/SC_it.c` | 2026-10-09 user code message | 对话文本同步；有效代码与原基线一致，保留本次排版 |
| `User/main.c` | 2026-10-09 user code message | 保留历史文件头，按本次代码主体同步；HAL_LPD_Init 调用保持注释；补齐末尾换行 |

机器可读记录见 `source_manifest.json`，包含对应 SHA-256。整理包的全部文件哈希见根目录 `SHA256SUMS.txt`。
