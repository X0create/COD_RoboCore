# dji_c_f407：大疆 RoboMaster 开发板 C 型（STM32F407IGH6）

**状态：CubeMX 代码已生成（2026-10-12），平台层 `05_platform/*/*_stm32f4.c` 已写，`c-debug` 编译 0 警告；还没上板。**

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
每一步后面的“为什么”说明这样配置的原因，改别的板子时可以照着判断。

1. **Project Manager → Project**：Toolchain / IDE 选 **CMake**，其余不变（Project Name 已是 `dji_c`）。
   - 为什么：框架用 CMake 构建，CLion、命令行、CI 都靠它；Keil 工程以后由 `tools/keil_sync.py` 从同一组源文件生成，不需要 CubeMX 生成 MDK 工程。
2. **Project Manager → Code Generator**：勾选 “Generate peripheral initialization as a pair of '.c/.h' files per peripheral”。
   - 为什么：平台层要 `#include <can.h>`、`<usart.h>` 拿到 `hcan1`、`huart3` 这些句柄；不勾的话全部挤在 `main.c` 里，没有头文件可以引用。
3. **Middleware → FREERTOS**
   - Interface：**CMSIS_V2**（6.18.1 已没有 CMSIS_V1，打开时可能已被清掉，重新选上）。
     - 为什么：只是 CubeMX 生成 `freertos.c` 时用的外壳；框架里的任务都用 FreeRTOS 原生接口（`04_core/os/os.c`），选哪个都不影响框架。
   - Tasks and Queues：**第一个任务（INS）删不掉，双击它改成启动任务**：名字 `startup`，优先级 `osPriorityRealtime7`，
     Stack Size `512`（字），Entry Function `startup_task`，Code Generation Option **As weak**，Allocation **Static**，
     Buffer Name `startup_task_stack`，Control Block Name `startup_task_tcb`。然后删掉其余 4 个任务（Vision、Control、CAN、Detect）。
     - 为什么：CubeMX 规定至少留一个任务，所以只能改不能删。其余任务都在 `01_applic/config/robot.c` 的任务表里由框架创建，改任务不用打开 CubeMX、也不怕重新生成被覆盖；
       `As weak` 让 CubeMX 只生成一个空的弱定义，框架在 `app_main.c` 里写真正的 `startup_task` 替换它；
       最高优先级保证它在其他任务之前先跑完（允许解锁后删除自己）。
   - Config parameters：`TOTAL_HEAP_SIZE` = **1024**，`CHECK_FOR_STACK_OVERFLOW` = **Option2**，`RECORD_STACK_HIGH_ADDRESS` = **Enabled**。
     - 为什么：框架的任务栈、控制块全是静态数组，不从堆里分配，堆留 1 KB 就够（原工程的 24 KB 白占内存）；
       Option2 让 FreeRTOS 在任务切换时检查栈有没有写穿，写穿了就进故障处理、整板复位；
       记录栈的最高地址后，调试器的 FreeRTOS 窗口才能显示每个任务的栈用量。
4. **System Core → SYS**：Timebase Source 保持 **TIM2**。
   - 为什么：SysTick 给 FreeRTOS 做 1 ms 节拍，HAL 自己的计时（`HAL_Delay`、各种超时）要另用一个定时器，否则两者抢同一个中断。
5. **Connectivity → USART3**（DBUS）：Mode **Asynchronous**；Baud Rate `100000`，Word Length **9 Bits（含校验位）**，
   Parity **Even**，Stop Bits 1，Data Direction **Receive Only**。DMA 里 USART3_RX 保持 **Circular**；NVIC 勾上 USART3 global interrupt。
   - 为什么：DR16 接收机发的是 DJI 规定的 DBUS 格式——100 kbit/s、8 个数据位、偶校验、1 个停止位（8E1），接收方必须一模一样；
     STM32 的字长要把校验位算进去，所以是 9 Bits。遥控只发不收，所以只接收，空出 PC10。
     循环 DMA + 空闲中断：字节由 DMA 自动收进缓冲区，一帧收完（线上空闲）时中断通知任务来取，CPU 不用逐字节处理。
6. **Connectivity → SPI1**：Prescaler 改为 **16**（5.25 MBits/s）。
   - 为什么：BMI088 的 SPI 最高 10 MHz，原来的 ÷8 是 10.5 MHz，略超规格，可能偶尔读错；一次只读十几个字节，降速后约 30 µs，不影响 1 kHz 的姿态解算。
7. **Timers → TIM5**：Channel1、Channel2、Channel3 都选 **PWM Generation CHx**（引脚自动是 PH10、PH11、PH12）；
   Prescaler `84-1`，Counter Period `255`。
   - 为什么：C 板的 RGB 灯接在这三路上，用 PWM 占空比调亮度，框架的状态灯接口每种颜色是 0–255，周期 255 正好一一对应；
     TIM5 时钟 84 MHz，÷84 后每 1 µs 计一次，256 个计数一个周期，约 3.9 kHz，眼睛看不出闪烁。
8. **Analog → ADC3**：勾选 **IN8**（PF10）。**先**在 DMA Settings 标签页点 Add 加 **ADC3**，Mode **Circular**，Data Width **Half Word**；
   **再**回到 Parameter Settings：Continuous Conversion Mode **Enabled**，DMA Continuous Requests **Enabled**（没加 DMA 时这一项只能选 Disable），
   ADC_Regular_ConversionMode → Rank 1 的 Channel 为 **Channel 8**、Sampling Time 选 **480 Cycles**。
   - 为什么：连续转换 + 循环 DMA 让 ADC 一直在后台采样、把最新结果写进内存，任务随时读、不用等转换完成（和 MC02 的做法一样）；
     电池电压经过大电阻分压，信号源内阻高，采样时间要长才能把采样电容充满，读数才准；结果是 12 位，用半字（16 位）存放。
9. **System Core → NVIC → Code generation**：列表里能看到的中断（CAN1 RX0 interrupts、CAN2 RX0 interrupts、USART3 global interrupt、
   各个 DMA stream global interrupt、USB On The Go FS global interrupt 等）**Call HAL handler** 都要勾上；优先级保持 5。
   TIM2 作为时基由 CubeMX 自动处理，这张表里可能看不到，不用管。
   - 为什么：不勾的话 CubeMX 生成的中断函数是空的，CAN、串口、DMA 都收不到数据，而且不报任何错；
     优先级数字不小于 5 的中断才能调用 FreeRTOS 的 `FromISR` 函数唤醒任务（`FreeRTOSConfig.h` 的规定），框架的接收全靠这个唤醒。
10. **不要**在 CubeMX 里打开 IWDG。
    - 为什么：CubeMX 会在 `main()` 一开始就启动看门狗，这时还在初始化，可能初始化没做完就被复位；
      框架在所有任务跑起来以后由 detect_task 直接写寄存器启动它，并按 control_task 的心跳来喂。
11. **GENERATE CODE**。生成后告诉我，我来核对生成结果、写平台层并编译。

USART1、USART6、TIM1、激光 PE11 等原工程里的配置暂时保留：框架不用它们，留着不影响；以后接裁判系统、图传时再按需要改。

## 重新生成后的检查

每次在 CubeMX 里生成代码后，逐条核对 `REGEN_CHECKLIST.md`。

## 和上层代码的接口

- `board.h`：上层要知道的板级接线——DR16 接哪个串口（USART3）、电池分压比（约 10.09，待核对）。MC02 目录里有同名文件。
- 生成代码里改过的地方都在 USER CODE 区（重新生成不会丢）：`freertos.c` 调用 `app_main()`、栈溢出钩子；
  `stm32f4xx_it.c` 四个 Fault 调用 `iwdg_fault()`；`FreeRTOSConfig.h` 的 `configASSERT`；`usbd_cdc_if.c` 的接收钩子。
- 链接脚本直接用 CubeMX 生成的 `STM32F407xx_FLASH.ld`：F407 没有 Cache，DMA 能访问普通 SRAM，不需要专用段。
- C 板的 FreeRTOS 是固件包自带的 V10.3.1（MC02 是 V10.6.2），`04_core/os/os.c` 按版本选用 `vTaskDelayUntil` / `xTaskDelayUntil`。

## 构建与烧录

- 构建：预设 **`c-debug`**（`cmake --preset c-debug && cmake --build --preset c-debug`），产物 `build/<系统>/c-debug/COD_RoboCore.elf`。
- 烧录、调试：Ozone 新建工程时 Device 选 **STM32F407IG**，其余和 MC02 相同（`docs/DEV_ENVIRONMENT.md`）。
- 框架不用 CMSIS-DSP（矩阵算法自写）。2026-10-17 已从 `.ioc` 和生成的 CMake 中移除原工程遗留的 DSP 依赖，
  干净克隆无需本地的 `libarm_cortexM4lf_math.a`。这次为文本修改，尚未在 CubeMX GUI 中打开并重新生成；再生成后核对清单第 15 项。
