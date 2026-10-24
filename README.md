# TL2 T13 — 2026-10-24 修正代码基准

当前分支 `baseline/t13-reviewed-20261024` 保存本轮已确认的 T13 修复及分层应用日志，以 T13 原始基线 `dc3d8728c36973180560d4c14aaafdf82669a8ed` 为父提交。源码修复、验证范围和仍待处理事项见 [当前基准说明](docs/T13_CURRENT_BASELINE_2026-10-24.md)。

| 版本 | 保存位置 | 工程入口 |
| --- | --- | --- |
| T13 当前修正基准 | 当前分支 `baseline/t13-reviewed-20261024` | [TL2_T13/Keil_C/Project/TL2_6.uvproj](TL2_T13/Keil_C/Project/TL2_6.uvproj) |
| T13 原始归档 | [baseline/t13-wifi-cen](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/baseline/t13-wifi-cen) | [原始基线说明](docs/T13_WIFI_CEN_BASELINE_2026-10-23.md) |
| T12 原基准 | [main](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/main) | [TL2_T12](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/main/TL2_T12) |

本轮整合水位秒任务、RX 环形队列、休眠阶段 UART 故障恢复、连续 APP 开关目标覆盖、首次校时重试、耗材复位基准、上报失败补报、跨年耗材结算，以及应用日志分层开关和已实际输出的 Wi-Fi 休眠诊断。

- T13 代码和工程位于 `TL2_T13/`。根目录旧源码镜像与旧状态文档仍属于 T12 历史资料。
- 原始压缩包 [archive/TL2_T13_wifi_cen_original.7z](archive/TL2_T13_wifi_cen_original.7z) 保持原字节。
- [本分支 T13 校验清单](T13_REVIEWED_20261024_SHA256SUMS.txt) 覆盖当前 548 个工程文件及原始压缩包。旧 `T13_WIFI_CEN_SHA256SUMS.txt` 对应原始归档分支。
- 用户已逐项反馈相关板级验证；本次远程整合未执行 Keil C51 重编译。保留的 `.obj`、`.lst`、`.hex` 等编译输出均来自原始快照，不能作为本提交构建产物。
- 缺水旁路、固定 25℃ 温度等原有调试配置继续保留。UART0 调试模式仍占用 P1.3 并屏蔽 UV 物理输出；关闭应用日志不会自动切换引脚用途。

水泵稳定恢复候选、MCU 空闲清零原因诊断与空闲策略调整尚未完成用户验证，未纳入本基准。未知 DP 加固及蜂鸣器候选已由用户决定本轮不做。
