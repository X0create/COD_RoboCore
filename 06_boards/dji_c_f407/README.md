# dji_c_f407：大疆 RoboMaster 开发板 C 型（STM32F407IGH6）

**状态：CubeMX 工程待生成（`dji_c.ioc` 已放好，按下面的步骤在 CubeMX 里改完再生成代码）；平台层 `*_stm32f4.c` 未写。**

两块 C 板（云台板、底盘板）用同一个 CubeMX 工程：C 板的引脚是固定的，两块板的区别只在 `01_applic/` 的配置里。

## 来源

`dji_c.ioc` 复制自 COD_F4_Template（`Gimbal/Gimbal.ioc`，提交 `1245b2b`，CubeMX 6.12.0，FW_F4 V1.28.1），
只改了工程名（`ProjectName`、`ProjectFileName`）并统一为 LF 换行。下面的修改由你在 CubeMX 里完成。

## 板上资源（C 板固定接线）

| 用途 | 外设 | 引脚 | 说明 |
| --- | --- | --- | --- |
| BMI088 | SPI1 | PB3 SCK、PB4 MISO、PA7 MOSI；PA4 加速度计片选、PB0 陀螺仪片选 | |
| IMU 加热 | TIM10 CH1 | PF6 | |
| DR16 遥控（板上 DBUS 口） | USART3 RX | PC11 | 板上已有反相电路 |
| CAN1 / CAN2 | bxCAN | PD0/PD1、PB5/PB6 | **只有经典 CAN**，没有 CAN FD |
| 蜂鸣器 | TIM4 CH3 | PD14 | |
| RGB 状态灯 | TIM5 CH1/CH2/CH3 | PH10 蓝、PH11 绿、PH12 红 | 原工程没配，要加 |
| 电池电压 | ADC3 IN8 | PF10 | 原工程没配，要加；分压比待万用表核对 |
| 视觉 USB | USB_OTG_FS | PA11/PA12 | 云台板用 |
| 串口 | USART1、USART6 | PA9/PB7、PG14/PG9 | 裁判系统、图传以后用 |

## 在 CubeMX 里要做的修改（按顺序）

用 CubeMX 6.18.1 打开 `06_boards/dji_c_f407/dji_c.ioc`。提示升级工程或下载 FW_F4 固件包时，都选同意。

1. **Project Manager → Project**
   - Toolchain / IDE：**CMake**。
   - 其余保持不变（Project Name 已是 `dji_c`）。
2. **Project Manager → Code Generator**：勾选 “Generate peripheral initialization as a pair of '.c/.h' files per peripheral”。
3. **Middleware → FREERTOS**
   - Interface：**CMSIS_V2**（6.18.1 已没有 CMSIS_V1，打开时可能已被清掉，重新选上）。
   - Tasks and Queues：**删掉全部 5 个任务**（INS、Vision、Control、CAN、Detect），只新建一个：
     名字 `startup`，优先级 `osPriorityRealtime7`，Stack Size `512`（字），Entry Function `startup_task`，
     Code Generation Option **As weak**，Allocation **Static**，Buffer Name `startup_task_stack`，
     Control Block Name `startup_task_tcb`。
   - Config parameters：`TOTAL_HEAP_SIZE` = **1024**，`CHECK_FOR_STACK_OVERFLOW` = **Option2**，
     `RECORD_STACK_HIGH_ADDRESS` = **Enabled**。
4. **System Core → SYS**：Timebase Source 保持 **TIM2**（SysTick 留给 FreeRTOS）。
5. **Connectivity → USART3**（DBUS）：Mode **Asynchronous**；Baud Rate `100000`，Word Length **9 Bits（含校验位）**，
   Parity **Even**，Stop Bits 1，Data Direction **Receive Only**。DMA 里 USART3_RX 保持 **Circular**；NVIC 勾上 USART3 global interrupt。
6. **Connectivity → SPI1**：Prescaler 改为 **16**（5.25 MBits/s；BMI088 最高 10 MHz，原来的 ÷8 是 10.5 MHz，略超）。
7. **Timers → TIM5**：Channel1、Channel2、Channel3 都选 **PWM Generation CHx**（引脚自动是 PH10、PH11、PH12）；
   Prescaler `84-1`，Counter Period `255`。
8. **Analog → ADC3**：勾选 **IN8**（PF10）；Continuous Conversion Mode **Enabled**；DMA Continuous Requests **Enabled**；
   Rank 1 的 Sampling Time 选 **480 Cycles**。DMA Settings 加 **ADC3**，Mode **Circular**，Data Width **Half Word**。
9. **System Core → NVIC → Code generation**：所有用到的中断（CAN1 RX0、CAN2 RX0、USART3、DMA、TIM2、OTG_FS 等）
   **Call HAL handler** 都要勾上；优先级保持 5（TIM2 时基为 15）。
10. **不要**在 CubeMX 里打开 IWDG：框架直接写寄存器启动看门狗。
11. **GENERATE CODE**。生成后告诉我，我来核对生成结果、写平台层并编译。

USART1、USART6、TIM1、激光 PE11 等原工程里的配置暂时保留，不影响框架。

## 重新生成后的检查

和 MC02 一样要有一张检查清单（`REGEN_CHECKLIST.md`），第一次生成成功后再写。
