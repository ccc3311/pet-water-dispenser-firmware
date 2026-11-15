# 低功耗 Wi-Fi 宠物饮水机固件

基于赛元 SC92L7533、涂鸦 Wi-Fi MCU SDK 和 SC89601 充电芯片的宠物饮水机项目。

**最新源码基线为 T13（2026-11-15）。** [T13 源码分支](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/baseline/t13-20261115) · [基线说明](https://github.com/ccc3311/pet-water-dispenser-firmware/blob/baseline/t13-20261115/docs/T13_BASELINE_2026-11-15.md)

## T12 归档

2026-10-21 同步桌面 `TL2_T12` 工程快照。完整工程入口为 [TL2_6.uvproj](TL2_T12/Keil_C/Project/TL2_6.uvproj)，保留桌面源码、工程配置、厂商库、生成器资料和软件履历表。原目录中的文件时间截至 2026-10-09；2026-10-21 为本次检查与上传日期。

本快照保留原始源码字节和调试设置。本轮完整重编译已尝试，但 C51 启动时出现 Windows 系统 DLL 重定位错误，未完成编译与链接，也未执行上板验收。检查详情见 [工程检查报告](docs/PROJECT_REVIEW_2026-10-21.md)。

## T12 快照配置

| 项目 | 桌面快照状态 |
| --- | --- |
| 缺水检测 | main 和 ADC 中 `DEBUG_IGNORE_WATER_EMPTY=1`，缺水检测被调试旁路 |
| 温度 | ADC 转换结果随后被 `cached_temperature_c = 25` 覆盖；温度读取返回固定 25℃，影响依赖真实温度的控制与上报 |
| MCU STOP | `APP_MCU_STOP_ENABLE=0`，主循环不主动进入 STOP |
| 充电调试 | `APP_CHARGE_DEBUG=1` |
| 水泵强制常转 | `DEBUG_PUMP_ALWAYS_ON=0` |
| 低压检测初始化 | main 中 `HAL_LPD_Init()` 仍被注释 |

以上按当前源码检查记录；本次同步未修改固件逻辑或调试宏。

## 目录与编译入口

| 路径 | 内容 |
| --- | --- |
| `TL2_T12/Keil_C/Project/` | 原始 Keil C51 工程、启动汇编；`TL2_6` 为构建目标 |
| `TL2_T12/Keil_C/User/` | 主循环、初始化、中断、业务状态机及头文件 |
| `TL2_T12/Keil_C/Hardware/` | 外设驱动及接口头文件 |
| `TL2_T12/Keil_C/PWR/` | 工程实际编译的低功耗驱动、SC89601、Wi-Fi 任务 |
| `TL2_T12/Keil_C/FWLib/` | 赛元驱动、寄存器头文件和预编译 `.LIB` |
| `TL2_T12/Keil_C/Wifi/mcu_sdk/` | 涂鸦协议和 MCU SDK |
| `TL2_T12/CFG/`、`TL2_T12/TL2_6.cgenx` | EasyCodeCube 配置、模板和芯片资料 |
| `TL2_T12/RD02 TL2软件履历表REV.01.xlsx` | 原始软件履历表 |
| 根目录 `User/`、`App/`、`HAL/`、`Drivers/`、`TuyaMCUSDK/` | 与桌面快照对应的 24 份源码镜像，保留已有浏览路径；构建使用上方完整工程 |
| `docs/` | 当前状态、来源、检查和依赖说明 |
| `archive/` | 历史原始上传与来源记录 |

工程目标为 SC92L7533、32MHz、`SC92L753x` 宏、Large 内存模型。用安装了赛元支持的 Keil C51 打开完整工程并 Rebuild；不能按普通桌面 C 或 ARM 工程编译。工程引用的 116 个文件均已找到，其中 54 个为 C/汇编编译单元。

工程中 `Hardware/hal_power.c` 和 `PWR/hal_power.c` 同时存在；`.uvproj` 实际选择 `PWR/hal_power.c`。根目录 `HAL/hal_power.c` 镜像也指向该实现，完整快照保留两份原文件。

## T12 快照更新记录

- 补齐完整 Keil 工程、业务/HAL 头文件、`tuya_type.h`、赛元驱动与库。
- SC89601 寄存器读取会传播 STOP 阶段的返回值；OTG 准备增加故障寄存器读取和输入存在判断，输入识别纳入 P50、PG_STAT 和状态寄存器，并增加 OTG 确认诊断输出。
- ADC 中增加温度固定为 25℃的调试赋值，已在检查报告中标明。
- 同步已有 24 份源码镜像，保留原始编码、换行及版权声明；历史附件保留在 `archive/`。

上传包含 395 份桌面工程文件；排除编译产物、列表文件、个人 IDE 状态、备份与临时文件。SHA-256 完整性记录见 `SHA256SUMS.txt`，文件对应关系见 [SOURCE_MAP.md](docs/SOURCE_MAP.md)。厂商/SDK 声明见 [NOTICE.md](NOTICE.md)。
