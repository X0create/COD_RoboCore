# 相对 COD-H7-Template 的变更记录

本模板在同一块达妙 DM-MC02（STM32H723VGT6）上，以 [COD-H7-Template](https://github.com/GrassFanWang/COD-H7-Template)
（提交 `4622556`，MIT）为首要参考之一。这里记录**与它不同的地方**、原因和验证层级，方便熟悉旧工程的队员对照。

- 新的变更加在对应小节的最上面，写日期（`YYYY-MM-DD`）。
- 验证层级：**配置**（只改了设置）、**编译**、**主机测试**、**硬件实测**。没有写“硬件实测”的条目，都还没在板子上验证过。
- 设计理由的详细说明在 `docs/ARCHITECTURE.md`（《架构设计》）对应的 ADR 里。

## 已完成

### 板级配置（`boards/dm_mc02_h723/dm_mc02.ioc`）

| 日期 | 项目 | COD-H7-Template | 本模板 | 原因 | 验证 |
| --- | --- | --- | --- | --- | --- |
| 2026-09-28 | 系统时钟 | SYSCLK 640 MHz（PLL1：÷6 ×160 ÷1） | **550 MHz**（PLL1：÷3 ×68.75 ÷1，照搬 UniC） | 640 MHz 超出 H723 手册上限，CubeMX 6.18.1 也判为无效值（ADR 0028） | 配置：CubeMX 时钟树无报错 |
| 2026-09-28 | AXI / AHB 时钟 | 160 MHz（HPRE ÷4） | **275 MHz**（HPRE ÷2） | 与 UniC 实测配置一致；AXI SRAM、DMA 访问更快 | 配置 |
| 2026-09-28 | APB1–4 时钟 | 80 MHz | **137.5 MHz**（定时器时钟 275 MHz） | 随 AHB 变化。**TIM3（IMU 加热 PWM）的频率会变**，写驱动时按新时钟重算分频 | 配置 |
| 2026-09-28 | Flash 等待周期 | 迁移后被 CubeMX 改成 0 | **3** | 275 MHz AXI 需要 3 个等待周期；为 0 会取指出错 | 配置 |
| 2026-09-28 | FDCAN 时钟 | PLL2 100 MHz，分频 5 / 14 / 5 | **不变** | 100 MHz 能同时整除 1 Mbit/s 与 CAN FD 数据段 5 Mbit/s（DM8009）；UniC 的 96 MHz 做不到 5 Mbit/s | 配置 |
| 2026-09-28 | PLL3、USB 时钟 | PLL3 80 MHz（SPI2），USB 用 HSI48 | **不变** | —— | 配置 |
| 2026-09-28 | FreeRTOS 配置 | 默认值；最小栈 2048 字，任务名长 64 | 栈溢出检测 2、记录栈顶地址（Ozone 显示栈大小）、FreeRTOS 堆 1 KB（CMSIS_V2 强制保留动态分配，框架不用）、打开 `xTaskDelayUntil` 与 `uxTaskGetStackHighWaterMark` | 静态分配为主；调试可见栈使用 | 配置：已改 `.ioc`，待 CubeMX 确认 |
| 2026-09-28 | 工具链 | MDK-ARM V5.32 | **CMake**（CubeMX 生成 CMake 工程） | ADR 0019 | 配置：CubeMX 中已确认 |
| 2026-09-28 | FreeRTOS 接口 | CMSIS-RTOS V1，内核 V10.3.1 | **CMSIS-RTOS V2**，内核 V10.6.2 | CubeMX 6.18.1 对 H7 已不支持 V1（迁移时会把 FreeRTOS 整个删掉）；框架直接用原生 API，不受影响（ADR 0025） | 配置：CubeMX 中已启用 |
| 2026-09-28 | FreeRTOS 任务 | CubeMX 中定义 4 个静态任务（INS、Control、CAN、Detect） | CubeMX 中**只定义一个启动任务** `startup`（静态、512 字、最高优先级、入口 `startup_task` 为弱定义，由框架实现，完成后删除自己）；其余任务由框架静态创建 | CMSIS_V2 下 CubeMX 至少要保留一个任务，把它用作启动任务（ADR 0025 修订） | 配置：CubeMX 中已确认 |
| 2026-09-27 | CubeMX / 固件包 | CubeMX 6.12.1，FW_H7 V1.11.2 | CubeMX 6.18.1，FW_H7 V1.13.0 | 本机安装的版本；与 UniC 相同 | 配置：已迁移 |
| 2026-09-27 | 工程名 | `COD_H7_Template` | `dm_mc02` | 按板子命名 | —— |

### 运行时基础（阶段 0）

| 日期 | 项目 | COD-H7-Template | 本模板 | 原因 | 验证 |
| --- | --- | --- | --- | --- | --- |
| 2026-09-28 | 时间基准 | `bsp_dwt.c`：DWT 计数 | `rm_time_now_us()`：DWT 先解锁（M7 的 LAR 软件锁），再扩展成 64 位微秒、不回绕；启动时自检计数器在走 | 全工程唯一时间源（ADR 0020）；锁住时写 DWT 会被悄悄丢弃 | 硬件：2026-09-28 跨过第一次回绕连续；扩展逻辑主机测试 |
| 2026-09-28 | 日志 | 无统一日志；调试靠 UART7 发 VOFA | SEGGER RTT（V8.58.0）日志，E/W/I/D 四级，每行带毫秒时间戳，整行一次写入 | 不占串口，多任务打日志不串行 | 硬件：2026-09-28 RTT 输出正常 |
| 2026-09-28 | CAN 收发 | `bsp_can.c`：每路一个全放行的掩码滤波器，收帧后在中断回调里直接解析 | `can_subscribe*()` 按精确 ID / ID 范围配置硬件范围滤波器，其余帧拒收；中断只把帧放进环形缓冲并唤醒 comm_rx 任务，订阅者回调在任务里执行；接收 FIFO 按 CubeMX 配置自动选（FDCAN2 为 FIFO1）；发送时短暂关中断，多个任务可以共用 | 掩码会多收帧（UniC `can-range-claim-not-mask`）；中断里不做业务 | 编译 + 主机测试；上板待 V30 |
| 2026-09-28 | CAN 滤波器数量 | 每路 1 个（全放行掩码） | 每路 16 个（`StdFiltersNbr`），一个订阅占一个 | 精确过滤需要按订阅分配滤波器 | 已改 `.ioc`，待重新生成 |
| 2026-09-28 | 串口接收 | `bsp_uart.c`：DMA 双缓冲 + 空闲中断，DR16 只接受正好 18 字节的帧，解析在中断回调里 | `uart_rx_start()` / `uart_read()`：循环 DMA + 空闲中断，中断只通知，取数和解析在任务里；写位置读 DMA 计数；溢出等错误后自动重新启动接收 | 中断里不做业务（运行时契约第 2 节）；DR16 断线重连时的溢出不会让串口永久停收 | 编译 + 主机测试（取数逻辑）；上板待接 DR16 |
| 2026-09-28 | DMA 缓冲区 | Keil 分散加载文件里的 `.AXI_SRAM` 段，变量上写 `__attribute__((section(".AXI_SRAM")))` | 自己的链接脚本 `dm_mc02.ld` 中的 `.dma_buf` 段（AXI SRAM，32 字节对齐，NOLOAD），用 `RM_DMA_BUF` 放入，只在 platform 层使用 | 与原工程同一思路（ADR 0021）；链接脚本不交给 CubeMX 管，重新生成不会丢段 | 链接：探测变量落在 0x24000000 |
| 2026-09-28 | 状态灯 | 未使用板载 WS2812 | SPI6（PA7）驱动 WS2812：正常时绿灯每秒闪两下，第一次亮 50 ms、第二次亮 25 ms | 一眼能看出固件在跑（UniC 的指示约定，时长按用户要求） | 硬件：2026-09-28 绿灯闪烁正常（50 / 25 ms 版本待复看） |
| 2026-09-28 | 启动流程 | `MX_FREERTOS_Init` 创建 4 个业务任务 | `MX_FREERTOS_Init` 的 USER CODE 区只调用 `app_main()`；框架按固定顺序初始化并静态创建任务；启动任务完成后删除自己 | 启动顺序集中在一处（运行时契约第 1 节） | 硬件：2026-09-28 启动顺序正确，startup 任务运行后删除自己 |

### 工程与代码规范

| 日期 | 项目 | COD-H7-Template | 本模板 | 原因 | 验证 |
| --- | --- | --- | --- | --- | --- |
| 2026-09-28 | NVIC 代码生成 | 各中断都调用 HAL 处理函数 | 同左（重新启用 FreeRTOS 后 CubeMX 把 18 个中断的 “Call HAL handler” 关掉了，已勾回） | 否则 TIM2 时基、DMA、SPI2 中断函数为空 | 生成：31 / 31 调用 HAL（`REGEN_CHECKLIST.md` 第 1 条） |
| 2026-09-27 | 构建 | Keil MDK（AC6） | **CMake + Ninja + arm-none-eabi-gcc 15.2.1**；Keil 以后再加 | 一套构建同时出固件和电脑测试（ADR 0019） | 编译：2026-09-28 固件编译通过，0 警告，FLASH 91384 B、DTCM 42400 B（只含 CubeMX 生成代码） |
| 2026-09-27 | 编译警告 | —— | 手写代码开 `-Wall -Wextra … -Werror`，有警告即失败 | 0 警告要求由编译器保证 | 编译 |
| 2026-09-27 | 单元测试 | 无 | Unity v2.7.0，电脑上运行 | 算法和协议解析能在电脑上测 | 主机测试 |
| 2026-09-27 | 文件编码 | 源码注释为 GBK | **UTF-8 + LF** | GBK 在 gcc、Git、clang-format 下乱码（ADR 0002） | —— |
| 2026-09-27 | 目录结构 | `BSP / Components / Application` | `platform / core / algorithm / devices / msgs / subsystems / robots / boards` | 分层单向依赖，芯片差异只在 platform（ADR 0017） | —— |

## 计划中（写到对应模块时处理）

以下来自 2026-09-25 阅读旧代码时发现的问题，**均未上板验证**。

| 旧工程位置 | 旧做法 | 本模板计划 |
| --- | --- | --- |
| `Control_Task.c` | `osDelay(1)` 相对延时，周期会漂移 | `rm_task_delay_until()` 绝对时刻延时 |
| `INS_Task.c` | EKF 用固定 `dt = 0.001f` | 用 `rm_time_now_us()` 实测 dt |
| `CAN_Task.c` | `DM_8009_Motor[2]` 连发两次、`[3]` 不发；使能帧之间 `osDelay(30)` 硬等 | 电机组统一打包；达妙命令按顺序发送、等反馈确认 |
| `Motor.c` | 达妙使能 / 失能 / 设零点帧发往反馈 ID（`0x11` 等），MIT 帧发往控制 ID | **疑点**：对照达妙手册确认，上板验证 |
| `Motor.c` | 角度折算到 ±180°，多圈信息丢失 | 设备层保留 `int32` 圈数 |
| `Remote_Control.c` | 约 200 ms 判遥控丢失；不检查通道范围 | 100 ms 超时；超范围整帧丢弃 |
| `Bmi088.c` | 陀螺零偏用写死的常数 | 上电静止标定（用标准差判定），结果存 Flash |
| `Referee_System.c` | 协议 v1.8.0；CRC 前不检查长度 | 协议 v2.0.0；先查长度再校验 |
| `MiniPC.c` | `CDC_Transmit_HS(Buff, sizeof(*Buff))` 只发 1 字节 | 按实际长度发送；USB CDC 保留为视觉默认通道（ADR 0027） |
| `Config.h` | IMU 轴映射、重力 9.718、弹道系数等是全局宏 | 安装旋转写进 `config.h` 结构体；视觉常数归 VisionLink 或上位机 |
| 安全逻辑 | 无统一的遥控丢失 / 电机离线处理 | 全车停（急停、遥控丢失、未解锁、IMU 未就绪）+ 机构停（本机构设备离线）（ADR 0026） |
| `bsp_adc.c` | ADC1 采电池电压（×11），没有使用者 | `devices/battery`，第一版只提示低电量（ADR 0027） |
| 板载外设 | 未配置 SPI6（WS2812 状态灯）、TIM12（蜂鸣器） | 按 UniC 实测补上 |
