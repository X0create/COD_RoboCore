# 05_platform/

外设接口（≈ 老模板 `BSP/`）。芯片差异全部关在这一层。**一个外设一个目录**，接口、芯片实现、辅助代码都在里面：

| 目录 | 接口 | 芯片实现 | 辅助代码（与芯片无关，电脑上测试） |
| --- | --- | --- | --- |
| `can/` | `can.h` | `can_stm32h7.c`（FDCAN，全放行，ADR 0049） | `can_dlc`（DLC 换算）、`can_rx_ring`（接收环形缓冲） |
| `uart/` | `uart.h` | `uart_stm32h7.c`（DMA 循环接收 + 空闲中断） | `dma_ring`（从 DMA 循环缓冲取数） |
| `usb_cdc/` | `usb_cdc.h` | `usb_cdc_stm32h7.c` | `byte_ring`（字节环形缓冲） |
| `time/` | `time.h` | `time_stm32h7.c`（DWT） | `cycle_extend`（32 位计数扩展成 64 位） |
| `status_led/` | `status_led.h` | `status_led_stm32h7.c`（SPI 驱动 WS2812） | `ws2812`（颜色编码） |
| `spi/` | `spi.h` | `spi_stm32h7.c` | |
| `pwm/` | `pwm.h` | `pwm_stm32h7.c` | |
| `adc/` | `adc.h` | `adc_stm32h7.c` | |
| `gpio/`、`flash/`、`iwdg/` | （规划） | 引脚与外部中断、片内 Flash 擦写、硬件看门狗 | |
| `stm32h7/` | | `dma_buf.h`：H7 各外设共用的 DMA 缓冲段（ADR 0021） | |
| `compiler.h` | 编译器属性（`RM_NODISCARD` 等） | | |

- 引用接口：`#include "05_platform/can/can.h"`。板上资源（spi 设备、pwm 通道）按用途命名，实现里用表对应到硬件；uart、can 按芯片编号（ADR 0033）。
- 每种芯片用同名函数实现同一组接口：芯片实现文件名是 `<外设>_<芯片>.c`，CMake 按 `RM_CHIP` 只编译当前主控那一份（没有函数指针表）。加 F407 时，在每个外设目录里各加一个 `*_stm32f4.c`。
- 辅助代码只给本层的芯片实现用，上层不要直接调用。电脑测试用的假实现在 `tests/host/fakes/`。
- 芯片实现里 include CubeMX / HAL 的头文件一律用尖括号（`#include <adc.h>`）：同目录有本模块自己的同名接口头文件，引号会先找到它。
- **可以** include：HAL，仅限芯片实现文件。**禁止** include：任何上层。
