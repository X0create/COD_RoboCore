# CubeMX 重新生成检查清单（dm_mc02_h723）

每次在 CubeMX 里 **GENERATE CODE** 之后逐条核对，全部通过再提交。CubeMX 会悄悄改掉它认为不属于用户的设置，
下面每一条都对应一次真实发生过的问题。

| # | 检查 | 命令或位置 | 期望 | 来源 |
| --- | --- | --- | --- | --- |
| 1 | 每个中断处理函数都调用了 HAL | 见下方命令 | 两个数字相等（当前均为 31） | 2026-09-28 本工程：重新启用 FreeRTOS 后 TIM2、SPI2 和 16 个 DMA 中断的 “Call HAL handler” 被关掉，函数体为空；UniC 出过同样的事故 |
| 2 | HAL 时基是 TIM2，且 `TIM2_IRQHandler` 调用了 `HAL_TIM_IRQHandler(&htim2)` | `Core/Src/stm32h7xx_it.c`、`stm32h7xx_hal_timebase_tim.c` | 存在 | TIM2 处理函数为空时 `HAL_GetTick()` 不增长，板子不动 |
| 3 | CubeMX 只创建了一个任务 `startup`，入口 `startup_task` 是 `__weak` | `grep -n "osThreadNew\|__weak" Core/Src/freertos.c` | 只有一处 `osThreadNew`；`startup_task` 为 `__weak` | ADR 0025 修订：不允许出现 `defaultTask` 或其他 CubeMX 任务 |
| 4 | `MX_FREERTOS_Init()` 的 USER CODE 区里仍然调用 `app_main()` | `Core/Src/freertos.c` 的 `USER CODE BEGIN RTOS_THREADS` | 存在（接入框架后） | 生成器清空 USER CODE 区时框架入口会消失，固件能编译但什么都不做 |
| 5 | 系统时钟 550 MHz、VOS0、Flash 等待 3 | `Core/Src/main.c` 的 `SystemClock_Config()` | `PLLM = 3`、`PLLN = 68`、`PLLFRACN = 6144`、`FLASH_LATENCY_3` | ADR 0028；迁移时 CubeMX 曾把等待周期改成 0 |
| 6 | FDCAN1/2/3 标称速率 1 Mbit/s，FDCAN2 为 FD + BRS；每路 16 个标准滤波器 | `grep -E "CalculateBaudRate|StdFiltersNbr" dm_mc02.ioc`，以及 `Core/Src/fdcan.c` | `CalculateBaudRateNominal=1000000` ×3；`StdFiltersNbr = 16` ×3 | UniC 出过 960 kbit/s；滤波器不够时 `can_subscribe*()` 返回 false |
| 7 | MPU 区域 0：`0x24000000` 起 512 KB 不可缓存 | `Core/Src/main.c` 的 `MPU_Config()` | 存在 | ADR 0021 |
| 8 | SPI2、SPI6 数据位 8 bit；SPI6 内核时钟 HSE、分频 4 | `grep -E "SPI[26].DataSize|SPI6.BaudRate|SPI6CLockSelection" dm_mc02.ioc` | `SPI_DATASIZE_8BIT` ×2、`SPI_BAUDRATEPRESCALER_4`、`RCC_SPI6CLKSOURCE_HSE`（SPI6 任一项错，WS2812 乱色且无报错） | `.ioc` 缺键时会默认成 4 bit（UniC） |
| 9 | FreeRTOS 配置 | `Core/Inc/FreeRTOSConfig.h` | `configCHECK_FOR_STACK_OVERFLOW 2`、`configRECORD_STACK_HIGH_ADDRESS 1`、`configTOTAL_HEAP_SIZE 1024` | 本工程设定 |
| 10 | CubeMX 生成的链接脚本有没有变 | `git diff --ignore-cr-at-eol STM32H723xG_flash.ld` | 无变化；有变化时对照合并进本项目的 `dm_mc02.ld` | 构建只用 `dm_mc02.ld`，生成器改不到它；但芯片内存布局等真改动要人工合并（UniC 出过自定义段被生成器删掉） |

第 1 条的命令（在本目录执行）：

```bash
grep -cE '^void [A-Za-z0-9_]+_IRQHandler\(void\)' Core/Src/stm32h7xx_it.c | xargs echo "中断函数："
grep -cE '^\s*HAL_[A-Za-z_]+_IRQHandler\s*\(' Core/Src/stm32h7xx_it.c | xargs echo "调用 HAL："
```

不相等时：CubeMX → **System Core → NVIC → Code generation**，把缺少的中断勾上 **Call HAL handler**，保存后重新生成。
在 `.ioc` 里，这一项是 `NVIC.<中断>_IRQn=` 冒号分隔的第 9 个字段（启用 FreeRTOS 时共 10 个字段）；
也可以直接对比 `.ioc` 找出为 `false` 的那几行。
（不含 Cortex 内核异常，如 HardFault、MemManage，它们不调用 HAL。）
