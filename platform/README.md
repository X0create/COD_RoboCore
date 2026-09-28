# platform/

外设的抽象接口，以及每种芯片一份实现。芯片差异全部关在这一层。

| 目录 | 内容 |
| --- | --- |
| `include/platform/` | 接口头文件：can、uart、spi、gpio、pwm、adc、usb_cdc、time、flash、iwdg |
| `common/` | 各芯片实现共用的纯计算，只给 platform 内部用，在电脑上测试：`cycle_extend`（DWT 64 位）、`dma_ring`（DMA 循环缓冲取数）、`can_dlc`（DLC 换算）、`can_rx_ring`（CAN 接收环形缓冲）、`ws2812`（状态灯编码） |
| `stm32h7/` | H723 的实现（FDCAN，DMA 与 Cache 处理） |
| `stm32f4/` | F407 的实现（bxCAN） |
| `host/` | PC 上的假实现，供主机测试和仿真 |

- 每种芯片用同名函数实现同一组接口，CMake 只编译当前主控那一份（没有函数指针表）。
- **可以** include：HAL，仅限实现目录。
- **禁止** include：任何上层。
