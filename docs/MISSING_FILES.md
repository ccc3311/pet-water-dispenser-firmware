# 工程依赖与待补资料

更新日期：2026-10-21。旧版仓库曾仅包含整理源码；本次从桌面 `TL2_T12` 补齐实际工程。

## 已补齐

- `TL2_T12/Keil_C/Project/TL2_6.uvproj`、`STARTUP.A51` 和实际编译/链接配置。
- `SC_Init.h`、`SC_it.h`、`HeadFiles/`、`Drivers/SCDriver_List.h` 和业务全局定义。
- `app_fsm.h`、`app_wifi.h`、HAL 头文件和 `tuya_type.h`。
- `FWLib/SC92F_Lib/` 中的赛元驱动源码、SC92L753x 寄存器定义、预编译 `.LIB`。
- 软件履历表和 EasyCodeCube CFG 模板。

静态检查确认工程列出的 116 个文件以及 Include Path 目录均存在。请用完整工程路径构建；根目录的 24 份源码是阅读镜像，并未另造第二套工程。

## 仍需环境或外部资料

- 可正常启动的 Keil C51 工具链及赛元设备/下载调试支持。标准 `stdio.h`、`intrins.h` 等由 Keil 提供。
- 本轮 C51 发生 Windows DLL 重定位错误，需在正常工具链环境完成 Rebuild，再确认编译与链接结果。
- 与当前板版本对应的硬件原理图、实际测量数据及本轮上板测试记录。
- 如果启用 `CONFIG_TUYA_ENABLE_TEST`，还需 `dp_define.code_snippet`、`dp_handle.code_snippet`、`dp_id.code_snippet`、`dp_id_handle.code_snippet`、`dp_var.code_snippet`。当前配置未启用该测试宏。

`.cgenx` 内保留生成时的历史绝对路径，移机后需在 EasyCodeCube 中重新指定工程位置。Keil 主工程引用主要采用相对路径。
