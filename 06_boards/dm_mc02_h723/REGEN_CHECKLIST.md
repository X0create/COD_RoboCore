# CubeMX 重新生成检查清单（dm_mc02_h723）

每次在 CubeMX 里 **GENERATE CODE** 之后逐条核对，全部通过再提交。CubeMX 会悄悄改掉它认为不属于用户的设置，
下面每一条都对应一次真实发生过的问题。

| # | 检查 | 命令或位置 | 期望 | 来源 |
| --- | --- | --- | --- | --- |
| 1 | 每个中断处理函数都调用了 HAL | 见下方命令 | 两个数字相等（当前均为 31） | 2026-09-28 本工程：重新启用 FreeRTOS 后 TIM2、SPI2 和 16 个 DMA 中断的 “Call HAL handler” 被关掉，函数体为空 |
| 2 | HAL 时基是 TIM2，且 `TIM2_IRQHandler` 调用了 `HAL_TIM_IRQHandler(&htim2)` | `Core/Src/stm32h7xx_it.c`、`stm32h7xx_hal_timebase_tim.c` | 存在 | TIM2 处理函数为空时 `HAL_GetTick()` 不增长，板子不动 |
| 3 | CubeMX 只创建了一个任务 `startup`，入口 `startup_task` 是 `__weak` | `grep -n "osThreadNew\|__weak" Core/Src/freertos.c` | 只有一处 `osThreadNew`；`startup_task` 为 `__weak` | 任务都由框架的任务表创建，不允许出现 `defaultTask` 或其他 CubeMX 任务 |
| 4 | `MX_FREERTOS_Init()` 的 USER CODE 区里仍然调用 `app_main()` | `Core/Src/freertos.c` 的 `USER CODE BEGIN RTOS_THREADS` | 存在（接入框架后） | 生成器清空 USER CODE 区时框架入口会消失，固件能编译但什么都不做 |
| 5 | 系统时钟 550 MHz、VOS0、Flash 等待 3 | `Core/Src/main.c` 的 `SystemClock_Config()` | `PLLM = 3`、`PLLN = 68`、`PLLFRACN = 6144`、`FLASH_LATENCY_3` | 550 MHz 是 H723 手册最高频率；迁移时 CubeMX 曾把等待周期改成 0，Flash 读错会直接跑飞 |
| 6 | FDCAN1/2/3 标称速率 1 Mbit/s；**FDCAN1、FDCAN2 为经典帧，FDCAN3 为 FD + BRS**（数据段预分频 1、TSeg1 14、TSeg2 5，即 5 Mbit/s），FDCAN3 的 Rx FIFO0 与 Tx 每格 64 字节；每路 16 个标准滤波器 | `grep -E "CalculateBaudRate|StdFiltersNbr|FrameFormat|ElmtSize" dm_mc02.ioc`，以及 `Core/Src/fdcan.c` | `CalculateBaudRateNominal=1000000` ×3；`StdFiltersNbr = 16` ×3；FDCAN3 `FDCAN_FRAME_FD_BRS`、`RxFifo0ElmtSize`/`TxElmtSize` = `FDCAN_DATA_BYTES_64` | 分频算错时速率会变成 960 kbit/s 这类近似值，和电机通信不上 |
| 7 | MPU 区域 0：`0x24000000` 起 512 KB 不可缓存 | `Core/Src/main.c` 的 `MPU_Config()` | 存在 | DMA 缓冲区在这一段；可缓存的话 CPU 读到的是缓存里的旧数据 |
| 8 | SPI2、SPI6 数据位 8 bit；SPI6 内核时钟 HSE、分频 4 | `grep -E "SPI[26].DataSize|SPI6.BaudRate|SPI6CLockSelection" dm_mc02.ioc` | `SPI_DATASIZE_8BIT` ×2、`SPI_BAUDRATEPRESCALER_4`、`RCC_SPI6CLKSOURCE_HSE`（SPI6 任一项错，WS2812 乱色且无报错） | `.ioc` 缺这一项时会默认成 4 bit |
| 9 | FreeRTOS 配置 | `Core/Inc/FreeRTOSConfig.h` | `configCHECK_FOR_STACK_OVERFLOW 2`、`configRECORD_STACK_HIGH_ADDRESS 1`、`configTOTAL_HEAP_SIZE 1024` | 本工程设定 |
| 10 | CubeMX 生成的链接脚本有没有变 | `git diff --ignore-cr-at-eol STM32H723xG_flash.ld` | 无变化；有变化时对照合并进本项目的 `dm_mc02.ld` | 构建只用 `dm_mc02.ld`，生成器改不到它；但芯片内存布局等真改动要人工合并（直接改生成的链接脚本，自定义段会被下次生成删掉） |
| 11 | USB CDC 接收钩子还在 | `grep -n "usb_cdc_rx_isr" USB_DEVICE/App/usbd_cdc_if.c` | 两处：`USER CODE BEGIN PV` 里的声明、`USER CODE BEGIN 11`（`CDC_Receive_HS`）里的调用 | 没有钩子时 USB 能枚举但收不到任何数据；`MX_USB_DEVICE_Init()` 由框架的 `usb_cdc_start()` 调用，CubeMX 弱定义 `startup_task` 里那句不会执行 |
| 12 | 生成时 Toolchain 是 CMake；若刚按 MDK-ARM 生成过，已改回 CMake 再生成一次，并运行了 `python3 tools/keil_sync.py` | `grep TargetToolchain dm_mc02.ioc`；`ls Middlewares/Third_Party/FreeRTOS/Source/portable/GCC`；`python3 tools/keil_sync.py --check` | `TargetToolchain=CMake`；`GCC` 目录存在；输出“Keil 工程是最新的” | CubeMX 每次生成会删掉另一种工具链的文件 |
| 13 | 致命故障处理还接在 IWDG 上 | `grep -n "iwdg_fault" Core/Src/stm32h7xx_it.c Core/Src/freertos.c Core/Inc/FreeRTOSConfig.h` | `stm32h7xx_it.c` 5 处（声明 + HardFault / MemManage / BusFault / UsageFault 的 USER CODE 0 区）；`freertos.c` 2 处（声明 + `vApplicationStackOverflowHook`）；`FreeRTOSConfig.h` 2 处（`USER CODE BEGIN 1` 里的声明和 `configASSERT`） | 生成器清掉后故障时又变成死循环，车停发但不复位 |
| 14 | 零点开关引脚是 GPIO 输入（只在接了舵轮零点开关时需要） | `grep -E "^(PE13|PE9|PA2|PA0)\." *.ioc` | 4 个引脚都是 `GPIO_Input`（上下拉按开关型号定；开关模块自带上拉电路时不开内部上下拉），对应 `05_platform/gpio/gpio_stm32h7.c` 的 `GPIO_IN_ZERO_SWITCH_1–4` | 没配时引脚是模拟模式，读出恒为低电平，找零会超时、这个轮子不出力 |

第 1 条的命令（在本目录执行）：

```bash
grep -cE '^void [A-Za-z0-9_]+_IRQHandler\(void\)' Core/Src/stm32h7xx_it.c | xargs echo "中断函数："
grep -cE '^\s*HAL_[A-Za-z_]+_IRQHandler\s*\(' Core/Src/stm32h7xx_it.c | xargs echo "调用 HAL："
```

不相等时：CubeMX → **System Core → NVIC → Code generation**，把缺少的中断勾上 **Call HAL handler**，保存后重新生成。
在 `.ioc` 里，这一项是 `NVIC.<中断>_IRQn=` 冒号分隔的第 9 个字段（启用 FreeRTOS 时共 10 个字段）；
也可以直接对比 `.ioc` 找出为 `false` 的那几行。
（不含 Cortex 内核异常，如 HardFault、MemManage，它们不调用 HAL。）
