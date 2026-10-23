# TL2 T13 — 带 Wi-Fi CEN 版本基线

本分支 `baseline/t13-wifi-cen` 保存 2026-10-23 用户提供的 `TL2_T13(1).7z` 原始快照，作为带 Wi-Fi CEN 恢复流程的 T13 基线。固件逻辑、调试宏、源文件编码及工程文件均按附件原样保留；本次归档未修复审查发现的问题。

| 版本 | 保存位置 | 工程入口 |
| --- | --- | --- |
| T13，带 Wi-Fi CEN | 当前独立分支 `baseline/t13-wifi-cen` | [TL2_T13/Keil_C/Project/TL2_6.uvproj](TL2_T13/Keil_C/Project/TL2_6.uvproj) |
| 原 T12 | [main 分支](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/main)，本次归档时提交为 `759b5d255c103c7ea3eb54a57930132750b1f6ab` | [T12 固定基线](https://github.com/ccc3311/pet-water-dispenser-firmware/tree/759b5d255c103c7ea3eb54a57930132750b1f6ab/TL2_T12) |

- [T13 原始压缩包](archive/TL2_T13_wifi_cen_original.7z)：与用户上传文件字节一致，仅调整归档文件名。
- [T13 完整工程](TL2_T13/)：548 个文件，包含源码、CFG、工程配置、原有编译输出和 IDE 状态。
- [基线说明与待处理问题](docs/T13_WIFI_CEN_BASELINE_2026-10-23.md)。
- [T13 SHA-256 校验清单](T13_WIFI_CEN_SHA256SUMS.txt)：覆盖原始压缩包及全部 548 个解压文件。

当前分支从 T12 提交派生，保留 `TL2_T12/` 及原有根目录源码镜像供历史对照；这些镜像仍属于 T12。查看或构建 T13 时请使用 `TL2_T13/` 内的工程。原 `main` 分支未更新。

T13 源码已包含 Wi-Fi CEN 拉低/释放及 UART 恢复状态机。本次未执行 Keil C51 完整重编译或上板测试；部分工程配置在附件中为不可直接解析的二进制内容，已原样保存。现有编译输出仅为附件内容，不代表本次验证结果。

后续开发请从此基线另建修复分支，以便保留可回溯的原始版本。
