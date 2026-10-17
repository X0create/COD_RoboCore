# CubeMX 重新生成检查清单（dji_c_f407）

每次在 CubeMX 里 **GENERATE CODE** 之后逐条核对，全部通过再提交。CubeMX 会悄悄改掉它认为不属于用户的设置，
很多项改错了照样能编译，但板子不工作、也不报错。命令都在本目录（`06_boards/dji_c_f407/`）下执行。

| # | 检查 | 命令或位置 | 期望 | 不对会怎样 |
| --- | --- | --- | --- | --- |
| 1 | 每个中断处理函数都调用了 HAL | 见下方命令 | 两个数字只差 1（EXTI9_5 一个函数里调用两次 HAL）；2026-10-12 首次生成为 15 / 16 | 中断函数是空的：CAN、串口、DMA 收不到数据 |
| 2 | HAL 时基是 TIM2，`TIM2_IRQHandler` 调用了 `HAL_TIM_IRQHandler(&htim2)` | `Core/Src/stm32f4xx_it.c`、`stm32f4xx_hal_timebase_tim.c` | 存在 | `HAL_GetTick()` 不增长，带超时的 HAL 函数卡死 |
| 3 | CubeMX 只创建了一个任务 `startup`，入口 `startup_task` 是 `__weak` | `grep -n "osThreadNew\|__weak" Core/Src/freertos.c` | 只有一处 `osThreadNew`；`startup_task` 为 `__weak` | 多出 CubeMX 任务，或和框架的 `startup_task` 冲突 |
| 4 | `MX_FREERTOS_Init()` 的 USER CODE 区里仍然调用 `app_main()` | `Core/Src/freertos.c` 的 `USER CODE BEGIN RTOS_THREADS` | 存在 | 能编译、能烧录，但框架的任务一个都没创建 |
| 5 | 系统时钟 168 MHz，APB1 42 MHz、APB2 84 MHz | `Core/Src/main.c` 的 `SystemClock_Config()` | `PLLM = 6`、`PLLN = 168`、`PLLP = 2`、`FLASH_LATENCY_5` | CAN 速率、定时器频率全部不对 |
| 6 | CAN1、CAN2 1 Mbit/s，硬件自动恢复 bus-off | `grep -E "Prescaler\|TimeSeg\|AutoBusOff" Core/Src/can.c` | 两路都是 `Prescaler = 3`、`CAN_BS1_10TQ`、`CAN_BS2_3TQ`、`AutoBusOff = ENABLE` | 速率不对时和电机通信不上；ABOM 关掉后 bus-off 不会恢复（`can_recover()` 在 F4 上不做事） |
| 7 | USART3（DBUS）100000、9 位、偶校验、只接收，RX DMA 循环 | `Core/Src/usart.c` | `BaudRate = 100000`、`UART_WORDLENGTH_9B`、`UART_PARITY_EVEN`、`UART_MODE_RX`；`hdma_usart3_rx.Init.Mode = DMA_CIRCULAR` | 遥控数据全是乱码，或 `uart_rx_start()` 返回 false |
| 8 | SPI1 分频 16 | `grep BaudRatePrescaler Core/Src/spi.c` | `SPI_BAUDRATEPRESCALER_16` | 10.5 MHz 超过 BMI088 的 10 MHz，偶尔读错 |
| 9 | FreeRTOS 配置 | `Core/Inc/FreeRTOSConfig.h` | `configTOTAL_HEAP_SIZE 1024`、`configCHECK_FOR_STACK_OVERFLOW 2`、`configRECORD_STACK_HIGH_ADDRESS 1` | 栈溢出不检查；堆白占内存 |
| 10 | TIM5（RGB 灯）周期 255 | `grep "htim5.Init" Core/Src/tim.c` | `Prescaler = 84-1`、`Period = 255` | 默认周期 4294967295 时灯几十分钟才变一次，看起来不亮 |
| 11 | ADC3 连续转换 + DMA 循环 | `Core/Src/adc.c` | `ContinuousConvMode = ENABLE`、`DMAContinuousRequests = ENABLE`、通道 8、480 周期；`hdma_adc3.Init.Mode = DMA_CIRCULAR` | 电池电压一直是上电那一刻的值 |
| 12 | USB CDC 接收钩子还在 | `grep -n "usb_cdc_rx_isr" USB_DEVICE/App/usbd_cdc_if.c` | 两处：`USER CODE BEGIN PV` 里的声明、`CDC_Receive_FS` 的 `USER CODE BEGIN 6` 里的调用 | USB 能枚举但收不到任何数据 |
| 13 | 致命故障处理还接在 IWDG 上 | `grep -n "iwdg_fault" Core/Src/stm32f4xx_it.c Core/Src/freertos.c Core/Inc/FreeRTOSConfig.h` | `stm32f4xx_it.c` 5 处（声明 + 四个 Fault 的 USER CODE 0 区）；`freertos.c` 2 处（声明 + `vApplicationStackOverflowHook`）；`FreeRTOSConfig.h` 2 处（声明和 `configASSERT`） | 故障时变成死循环：车停发但不复位 |
| 14 | 生成时 Toolchain 是 CMake | `grep TargetToolchain dji_c.ioc` | `TargetToolchain=CMake` | 按 Keil 生成会删掉 CMake 需要的文件 |
| 15 | 没有未使用的 CMSIS-DSP 链接依赖 | `rg -n 'ALGOBUILD|ARM/DSP|libarm_cortexM4lf_math' dji_c.ioc cmake/stm32cubemx/CMakeLists.txt` | 无匹配；`c-debug` 从干净克隆能链接 | 本地未跟踪的 DSP 库掩盖问题，换电脑或 CI 后链接失败 |

第 1 条的命令：

```bash
grep -cE '^void [A-Za-z0-9_]+_IRQHandler\(void\)' Core/Src/stm32f4xx_it.c | xargs echo "中断函数："
grep -cE '^\s*HAL_[A-Za-z_]+_IRQHandler\s*\(' Core/Src/stm32f4xx_it.c | xargs echo "调用 HAL："
```

不一致时：CubeMX → **System Core → NVIC → Code generation**，把缺少的中断勾上 **Call HAL handler**，保存后重新生成。
