# dm_mc02_h723：达妙 DM-MC02（STM32H723VGT6）

## 来源与许可

`dm_mc02.ioc` 以 COD-H7-Template（提交 `4622556`，CubeMX 6.12.1，FW_H7 V1.11.2）的 `COD_H7_Template.ioc` 为起点，
只改了工程名（`ProjectName`、`ProjectFileName`）并统一为 LF 换行。原工程许可如下：

```text
MIT License

Copyright (c) 2025 GrassFan_Wang
```

完整条款见 <https://github.com/GrassFanWang/COD-H7-Template/blob/main/LICENSE>。

## CubeMX 配置要点

改 CubeMX 配置、重新生成代码之后，逐条核对 `REGEN_CHECKLIST.md`。下面是这块板当前的关键配置和原因：

- **工具链选 CMake**：CMake 和 Keil 编的是同一组源文件，Keil 工程见下面“Keil”一节。
- **FreeRTOS（CMSIS_V2）只建一个启动任务 `startup`**（静态、512 字、`osPriorityRealtime7`，入口 `startup_task` 选 As weak，
  由框架实现）；其余任务在 `01_applic/config/robot.c` 的任务表里，不在 CubeMX 里定义任务和队列。
- **系统时钟 550 MHz**（H723 手册规定的最高频率，不超频）；PLL2 保持 100 MHz，FDCAN 分频 5 / 14 / 5
  （CAN FD 数据段 5 Mbit/s 需要 100 MHz 的内核时钟）。
- **HAL 时基用 TIM2**（SysTick 留给 FreeRTOS）；FDCAN1/2 为 1 Mbit/s 经典帧（接电机），FDCAN3 为 FD+BRS 且每格 64 字节（板间通信）；
  SPI2 数据位 8 bit（BMI088）。
- **SPI6 驱动 WS2812 状态灯**：Transmit Only Master，内核时钟 HSE 24 MHz，÷4 = 6 MHz，8 bit，CPHA 2 Edge，
  MOSI = PA7 Very High；CubeMX 同时占用 PA5 作 SCK，WS2812 不用。**TIM12 CH2**（PB15）驱动蜂鸣器。
生成代码在工作区是 CRLF 换行，提交时由 `.gitattributes` 转成 LF，不影响编译。

## Keil

`MDK-ARM/dm_mc02.uvprojx` 是 Keil 工程，用 Keil 自己的编译器（AC6）编译，和 CMake 编的是同一组源文件。Target 只有一个 `dm_mc02`。

**平时用：** 打开 `MDK-ARM/dm_mc02.uvprojx` → F7 编译 → F8 烧录（J-Link）→ Ctrl+F5 调试。不用跑任何脚本。

**加新的 `.c` 文件：** 在 CMake（对应层的 `CMakeLists.txt`）里加，同时在 Keil 里把文件拖进对应分组。
忘了加 Keil 那边时，`build/check.sh` 和 CI 会提示“Keil 工程不是最新”；也可以运行 `python3 tools/keil_sync.py` 让脚本补齐。

**CubeMX 的规则：**

1. 平时 CubeMX 只按 **CMake** 生成（`.ioc` 里 Toolchain 保持 CMake）。Keil 工程直接用 `Core/`、`Drivers/` 等 CubeMX 生成的文件，
   改了外设配置不用动 Keil 工程；只有开了新外设、CubeMX 多出新的 HAL 驱动文件时，在 Keil 里补加（或运行 `tools/keil_sync.py`）。
2. 只有需要重新生成 Keil 工程本身时，才把 Toolchain 改成 MDK-ARM 生成一次，**然后必须改回 CMake 再生成一次**，再运行
   `python3 tools/keil_sync.py`。原因：CubeMX 每次生成都会删掉另一种工具链专用的文件（2026-10-01 实测：按 CMake 生成删掉了 Keil 用的
   FreeRTOS `portable/RVDS` 目录和 `mdk/` 目录），而 CMake 构建需要 `portable/GCC`。

**和 CubeMX 原始 Keil 工程的区别**（`tools/keil_sync.py` 做的修改）：

- FreeRTOS 移植层用 `portable/GCC/ARM_CM4F`（AC6 应使用 GCC 移植层；CubeMX 选的 `RVDS` 是给 AC5 的，而且按 CMake 生成时会被删掉）；
- 链接用 `dm_mc02.sct`（与 `dm_mc02.ld` 同样的内存布局：DMA 缓冲区 `.dma_buf` 在 0x24000000），不用 Keil 自动生成的布局——
  自动布局会把 DMA 缓冲区放进 DTCM，DMA 访问不到，串口收不到数据也不报错；
- 加入 01–05 层的源文件，宏定义和头文件路径与 CMake 相同，另加 `RTT_USE_ASM=0`（RTT 用 C 版，不用 GNU 语法的汇编）；
- AC6、gnu11、-O0、每个函数单独一段；调试器 J-Link；输出到 `build/keil/`。

**验证状态：** 2026-10-01 `UV4 -b` 编译 0 错误（6 条警告都在 CubeMX 生成的代码里），map 里 DMA 缓冲区在 0x24000000、
`startup_task` 是框架的实现。**Keil 编出的固件尚未上板。**
