# 05_platform/

外设接口：整个仓库里只有这一层直接调用 HAL、操作寄存器，芯片差异全部关在这里，换芯片只改这一层。
目前有两种芯片：STM32H723（达妙 MC02，`*_stm32h7.c`）和 STM32F407（大疆 C 板，`*_stm32f4.c`），由预设选择。
**一个外设一个目录**，最多三个文件：接口 `<外设>.h`、与芯片无关的辅助代码 `<外设>.c`、芯片实现 `<外设>_<芯片>.c`。辅助代码只给芯片实现用、不该让上层看到时，声明放进内部头文件 `<外设>_internal.h`（第四个文件，目前只有 time）：

| 目录 | 接口 | 芯片实现 | 辅助代码 `<外设>.c`（与芯片无关，电脑上测试；声明在接口头文件末尾一节） |
| --- | --- | --- | --- |
| `can/` | `can.h` | `can_stm32h7.c`（FDCAN，全放行） | `can.c`：DLC 换算、接收环形缓冲 |
| `uart/` | `uart.h` | `uart_stm32h7.c`（DMA 循环接收 + 空闲中断） | `uart.c`：从 DMA 循环缓冲取数 |
| `usb_cdc/` | `usb_cdc.h` | `usb_cdc_stm32h7.c`（接收和发送完成均通知同一任务） | `usb_cdc.c`：字节环形缓冲 |
| `trace/` | `trace.h`（诊断事件格式与空宏） | `trace_stm32h7.c`（DWT / PRIMASK / NVIC） | `trace.c`：RAM 环和捕获状态机，仅诊断 / 主机测试编译 |
| `time/` | `time.h`（只有 `rm_time_init`、`rm_time_now_us`） | `time_stm32h7.c`（DWT） | `time.c`：32 位计数扩展成 64 位，声明在 `time_internal.h` |
| `status_led/` | `status_led.h` | `status_led_stm32h7.c`（SPI 驱动 WS2812） | `status_led.c`：WS2812 颜色编码 |
| `spi/` | `spi.h` | `spi_stm32h7.c` | |
| `pwm/` | `pwm.h` | `pwm_stm32h7.c` | |
| `adc/` | `adc.h` | `adc_stm32h7.c` | |
| `iwdg/` | `iwdg.h` | `iwdg_stm32h7.c`（IWDG1，直接写寄存器，不用 CubeMX） | |
| `gpio/` | `gpio.h`（按用途命名的输入引脚，轮询） | `gpio_stm32h7.c`（引脚模式由 CubeMX 配） | |
| `flash/` | （规划） | 片内 Flash 擦写 | |
| `stm32h7/` | | `dma_buf.h`：H7 各外设共用的 DMA 缓冲段（不可缓存，CPU 和 DMA 看到的数据一致） | |
| `compiler.h` | 编译器属性（`RM_NODISCARD` 等） | | |

- 引用接口：`#include "05_platform/can/can.h"`。板上资源（spi 设备、pwm 通道）按用途命名，实现里用表对应到硬件；uart、can 按芯片编号（它们在板子上有丝印编号，按编号接线更直观）。
- 每种芯片用同名函数实现同一组接口：芯片实现文件名是 `<外设>_<芯片>.c`，CMake 按 `RM_CHIP` 只编译当前主控那一份（没有函数指针表）。加一种新芯片时，在每个外设目录里各加一个 `*_<芯片>.c`（F407 就是这样加的）。
- 辅助代码只给本层的芯片实现用，上层不要直接调用。电脑测试用的假实现在 `tests/host/fakes/`。
- 芯片实现里 include CubeMX / HAL 的头文件一律用尖括号（`#include <adc.h>`）：同目录有本模块自己的同名接口头文件，引号会先找到它。
- **可以** include：HAL，仅限芯片实现文件。**禁止** include：任何上层。
