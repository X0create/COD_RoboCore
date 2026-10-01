# 05_platform/

外设的抽象接口，以及每种芯片一份实现。芯片差异全部关在这一层。

| 目录 | 内容 |
| --- | --- |
| `*.h`（本目录） | 接口头文件，以 `#include "05_platform/can.h"` 的形式引用：can、uart、spi、gpio、pwm、adc、usb_cdc、time、flash、iwdg。板上资源（spi 设备、pwm 通道）按用途命名，实现里用表对应到硬件；uart、can 按芯片编号（ADR 0033） |
| `common/` | 各芯片实现共用的纯计算，只给 platform 内部用，在电脑上测试：`byte_ring`（字节环形缓冲，USB 接收）、`cycle_extend`（DWT 64 位）、`dma_ring`（DMA 循环缓冲取数）、`can_dlc`（DLC 换算）、`can_rx_ring`（CAN 接收环形缓冲）、`ws2812`（状态灯编码） |
| `stm32h7/` | H723 的实现（FDCAN，DMA 与 Cache 处理） |
| `stm32f4/` | F407 的实现（bxCAN） |
| `host/` | PC 上的假实现（目前放在 `tests/host/fakes/`） |

- 每种芯片用同名函数实现同一组接口，CMake 只编译当前主控那一份（没有函数指针表）。
- **可以** include：HAL，仅限实现目录。
- **禁止** include：任何上层。
