# 当前源码中的硬件映射

本表按当前初始化及 HAL 实现整理，实际走线仍以对应 PCB 为准。历史注释中旧引脚不覆盖当前执行代码。

| MCU 引脚 | 当前用途 | 代码依据 |
| --- | --- | --- |
| P0.0 | 外设供电控制 | `SC_Init.c`、`app_fsm.c`、`hal_touch.c` |
| P0.1 | 缺水输入 | `hal_adc.c`、`main.c` |
| P0.2 | 水泵控制 | `hal_pwm.c` |
| P0.3 | 充电 IC 状态输入 | `SC_Init.c`、`main.c` |
| P0.4 / P0.5 | TM1650 SCL / SDA | `hal_tm1650.c` |
| P0.6 | 红外输入 | `hal_ir.c`、`hal_power.c` |
| P0.7 | 主触摸输入 | `hal_touch.c`、`hal_power.c` |
| P1.0 | 加热控制 | `hal_pwm.c` |
| P1.1 | UVC 按键输入，INT0 | `SC_Init.c` |
| P1.2 / P1.3 | UART0 调试串口配置 | `SC_Init.c` |
| P1.4 / P1.5 | USCI2 Wi-Fi UART 配置 | `SC_Init.c` |
| P1.6 / P1.7 | SC89601 SCL / SDA | `sc89601.c` |
| P2.0 | Wi-Fi 模组到 MCU 唤醒输入 | `hal_uart.c`、`hal_power.c` |
| P2.1 | MCU 到 Wi-Fi 保持唤醒输出，低有效 | `hal_uart.c` |
| P2.2 | 电池 ADC，AIN0 | `SC_Init.c`、`hal_adc.c` |
| P2.3 | TDS ADC，AIN1 | `SC_Init.c`、`hal_adc.c` |
| P2.4 / P2.5 | TDS 双向激励 | `hal_adc.c` |
| P2.6 | 蜂鸣器 | `hal_buzzer.c` |
| P2.7 | 温度 ADC，AIN5 | `SC_Init.c`、`hal_adc.c` |
| P5.0 | Type-C 检测，低有效 | `SC_Init.c`、`hal_power.c` |
| P5.1 | UVC 输出 | `SC_Init.c`、`hal_touch.c` |

Wi-Fi UART 当前按 32MHz、9600bps、10-bit 模式初始化。Timer0 当前使用 33536 重装值。所有参数均保持用户提交值。
