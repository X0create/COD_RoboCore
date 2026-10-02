# 中文逻辑伪代码

更新时间：2026-10-02。用中文把整个固件“从上电到发电机指令”的逻辑写一遍，**只讲做什么，不讲 C 语法**。
每一段后面的 `→ 文件:函数` 是对应的真实代码，在 IDE 里打开后用“跳到定义 / 查找用法”继续看细节（见 `docs/CALL_FLOW.md`）。
伪代码与代码不一致时以代码为准，并更新本文。

先看“分层与文件”知道每样东西放在哪，再从第 0 节开始按运行顺序读逻辑。

---

## 分层与文件

### 六层：谁调用谁

```
01_applic    应用层：这台车做什么（任务、安全门、底盘、姿态）            ≈ 老模板 Application/
    ↓ 只能往下调用
02_devices   设备层：每个外部设备的协议（电机、IMU、遥控……），换算成国际单位  ≈ Components/Device
03_algorithm 算法层：纯计算（PID、滤波、EKF、运动学），不碰硬件和 RTOS    ≈ Components/Algorithm、Controller
04_core      基础层：与设备无关的公共设施（任务创建、在线检测、日志、CRC）
05_platform  平台层：外设接口（CAN、串口、SPI、PWM……），芯片差异全关在这里  ≈ BSP/
06_boards    板级：CubeMX 生成的代码、链接脚本，每块板一个目录          ≈ Core/、Drivers/
```

规则：上层可以调用下层，下层不能调用上层；只有 05_platform 的芯片实现（`*_stm32h7.c`）和 06_boards 可以用 HAL。
03_algorithm、02_devices 的协议解析、01_applic 的安全门和机构都不碰硬件，能在电脑上测试（`tests/host/`，目录与六层一一对应）。
标“未接线”的文件已写好、有主机测试，但当前固件还没有调用它。

### 01_applic 应用层

| 文件 | 作用 |
| --- | --- |
| `system/app_main.c/.h` | 上电顺序：DWT 计时 → RTT 日志 → `objects_init()` → 按任务表建任务；`startup_task` 允许解锁后删除自己（第 1 节） |
| `system/safety_gate.c/.h` | 安全门：Init → Safe → Manual，决定这一周期是否全车停（第 6 节） |
| `config/params.h` | 这台车的全部可调参数：电机表、底盘尺寸与 PID、满杆速度、解锁拨杆、IMU 安装方向 |
| `config/objects.c/.h` | 这台车的全部对象（遥控、IMU、电机、底盘）和 `objects_init()` |
| `config/task_table.c` | 任务表：6 个任务的名字、入口、优先级、栈 |
| `tasks/ins_task.c/.h` | 1 ms：初始化 BMI088，循环调用 `ins_step()`，把事件写进日志（第 4 节） |
| `tasks/comm_rx_task.c/.h` | 收到数据就运行：打开 CAN、串口接收，把数据交给电机、DR16 解析；CAN bus-off 恢复（第 3 节） |
| `tasks/control_task.c/.h` | 1 ms：读输入 → 安全门 → 底盘 → 发电机指令（第 5 节） |
| `tasks/detect_task.c/.h` | 10 ms：打印设备上线 / 离线，只报告不停车（第 9 节） |
| `tasks/indicator_task.c/.h` | 25 ms：状态灯、蜂鸣器、电池电压与低电量（第 9 节） |
| `tasks/log_task.c/.h` | 1 s：通过 RTT 打印整车状态（第 9 节） |
| `modules/chassis/chassis.c/.h` | 底盘：读实测 → 算目标（斜坡、运动学逆解）→ 各轮速度环 PID 出力矩（第 7 节） |
| `modules/ins/ins.c/.h` | 惯性导航：读 BMI088 → 加热 → 零偏标定 → 安装旋转 → 低通 → EKF → 保存最新姿态，`ins_read()` 读 |
| `modules/gimbal/`、`shooter/`、`leg/`、`arm/` | 规划中，只有 README |

### 02_devices 设备层

| 文件 | 作用 |
| --- | --- |
| `motor/motor.c/.h` | 电机统一接口：收反馈、读快照、写力矩 / 停机动作，查某个 CAN ID 是否被电机占用；单位是输出轴的 rad、rad/s、N·m |
| `motor/motor_group.c/.h` | 电机组：确定每个电机最终发什么，按控制帧打包发送（第 8 节） |
| `motor/dji_motor.c/.h` | DJI 电机（M3508 / M2006 / GM6020）协议：反馈解码、控制帧编码 |
| `motor/dm_motor.c/.h` | 达妙电机 MIT 协议：MIT 帧、使能 / 失能 / 清错命令、反馈解码 |
| `imu/bmi088.c/.h` | BMI088 驱动：寄存器配置、读加速度 / 陀螺 / 温度；加热 PID → PWM 占空比 |
| `remote/dr16.c/.h` | DR16 遥控：分帧、校验、解析，保存最新一帧，`dr16_read()` 读（200 ms 没新帧算丢失） |
| `remote/vt_link.c/.h` | 图传链路：VT13 遥控帧和键鼠帧解析（未接线） |
| `referee/referee_frame.c/.h` | 裁判系统 0xA5 帧的检查，裁判系统和图传共用（裁判系统本身未接线） |
| `vision/vision_frame.c/.h` | 与上位机通信的 0x5A 帧：检查与组帧 |
| `vision/vision_link.c/.h` | 上位机链路：从字节流里找出 0x5A 帧（未接线，等视觉组定协议） |
| `battery/battery.c/.h` | 电池电压换算和低电量判定（带持续时间和回差） |
| `buzzer/buzzer.c/.h` | 蜂鸣器：按音符序列播放，不阻塞 |
| `board_link/board_link.c/.h` | 板间 CAN 通信：一条消息一个 ID，帧头序号 + 数据年龄，收到后存下、超时离线；编码小工具（未接线，单板车用不到） |
| `supercap/`、`actuator/` | 规划中（超级电容、舵机 / 气泵），只有 README |

### 03_algorithm 算法层

| 文件 | 作用 |
| --- | --- |
| `control/pid.c/.h` | 位置式 / 增量式 PID，不带 dt，有条件积分抗饱和；底盘速度环、IMU 加热在用 |
| `control/ramp.c/.h` | 斜坡：每次最多变化固定步长；底盘加速度限制在用 |
| `filter/lpf.c/.h` | 一阶、二阶低通；IMU 加速度低通在用 |
| `filter/kalman.c/.h` | 线性卡尔曼的五个步骤，EKF 的基础 |
| `attitude/quat_ekf.c/.h` | 四元数 EKF 姿态解算 |
| `attitude/gyro_bias.c/.h` | 陀螺零偏标定：静止采样，用标准差判断是否静止 |
| `kinematics/chassis_vel.h` | 底盘速度类型（vx、vy、wz），各运动学共用 |
| `kinematics/omni.c/.h` | 四轮全向轮运动学（当前这台车用的） |
| `kinematics/mecanum.c/.h` | 四轮麦轮运动学 |
| `kinematics/steer.c/.h` | 四轮舵轮运动学 |
| `kinematics/half_steer.c/.h` | 半舵半全向（对角两个舵轮 + 两个全向轮）运动学 |
| `math/matrix.c/.h` | 小矩阵加减乘、转置、求逆，代替 CMSIS-DSP |
| `math/math_const.h` | π 等数学常数，全仓库只在这里定义 |
| `power/rls.c/.h` | 带遗忘因子的递推最小二乘，给以后的功率模型辨识用（未接线） |
| `ballistic/` | 规划中（弹道解算），只有 README |

### 04_core 基础层

| 文件 | 作用 |
| --- | --- |
| `os/os.c/.h` | FreeRTOS 的薄封装：静态创建任务、按绝对时刻延时 |
| `os/critical.h` | 临界区（不含 FreeRTOS 头文件，电脑测试也能编译） |
| `os/delay.h` | 延时 |
| `watchdog/watchdog.c/.h` | 软件看门狗：判断设备最近有没有发来数据，并保管它最新的一份数据（不是硬件 IWDG） |
| `log/log.c/.h` | RTT 日志，E / W / I / D 四级，不支持 `%f` |
| `log/SEGGER_RTT_Conf.h`、`log/segger_rtt/` | SEGGER RTT 的配置和第三方源码 |
| `util/crc.c/.h` | 裁判系统协议的 CRC8 / CRC16 |
| `error/`、`param/` | 规划中（断言与错误码、Flash 参数存储），只有 README |

### 05_platform 平台层

每个外设一个目录：`xxx.h` 是接口，`xxx_stm32h7.c` 是 H723 的实现，其余是与芯片无关、电脑上可测的辅助代码。

| 文件 | 作用 |
| --- | --- |
| `can/can.h`、`can_stm32h7.c` | CAN / CAN FD 收发（FDCAN），接收中断把帧放进环形缓冲 |
| `can/can_rx_ring.c/.h` | CAN 接收环形缓冲：中断写、任务读 |
| `can/can_dlc.c/.h` | 数据字节数与 DLC 编码互相换算 |
| `uart/uart.h`、`uart_stm32h7.c` | 串口 DMA 循环接收 + 空闲中断 |
| `uart/dma_ring.c/.h` | 从 DMA 循环缓冲里取出新字节 |
| `spi/spi.h`、`spi_stm32h7.c` | SPI 阻塞传输，设备按用途命名（`SPI_DEV_IMU_ACCEL` 等） |
| `pwm/pwm.h`、`pwm_stm32h7.c` | PWM 输出，通道按用途命名（`PWM_IMU_HEATER` 等），设置占空比和频率 |
| `adc/adc.h`、`adc_stm32h7.c` | ADC DMA 连续采样，读引脚电压 |
| `time/time.h`、`time_stm32h7.c` | 全工程唯一的时间基准：上电以来的微秒数（DWT） |
| `time/cycle_extend.c/.h` | 把会回绕的 32 位周期计数扩展成 64 位 |
| `status_led/status_led.h`、`status_led_stm32h7.c` | 板载状态灯（MC02 是 SPI6 上的一颗 WS2812） |
| `status_led/ws2812.c/.h` | WS2812 颜色到 SPI 字节的编码 |
| `usb_cdc/usb_cdc.h`、`usb_cdc_stm32h7.c` | USB 虚拟串口，与上位机通信的默认通道（未接线） |
| `usb_cdc/byte_ring.c/.h` | 字节环形缓冲：中断写、任务读 |
| `stm32h7/dma_buf.h` | 把变量放进 DMA 专用段（不可缓存的 AXI SRAM） |
| `compiler.h` | 编译器属性（`RM_NODISCARD` 等） |
| `gpio/`、`flash/`、`iwdg/` | 规划中，只有 README |

### 06_boards 板级

| 文件 | 作用 |
| --- | --- |
| `dm_mc02_h723/dm_mc02.ioc` | CubeMX 工程：引脚、时钟、外设配置 |
| `dm_mc02_h723/Core/`、`USB_DEVICE/`、`Drivers/`、`Middlewares/` | CubeMX 生成的代码和 HAL、FreeRTOS 源码；只在 `USER CODE` 区内改 |
| `dm_mc02_h723/dm_mc02.ld` | 本项目用的链接脚本：由 CubeMX 的 `STM32H723xG_flash.ld` 复制，多了 DMA 段和检查，重新生成不会覆盖 |
| `dm_mc02_h723/REGEN_CHECKLIST.md` | 每次在 CubeMX 里重新生成代码后逐条核对 |
| `dji_c_f407/` | 规划中（大疆 C 板） |

### 仓库里的其他目录

| 目录 | 作用 |
| --- | --- |
| `CMakeLists.txt`、`cmake/` | 构建：固件和主机测试的入口、工具链、警告选项、板子选择 |
| `tests/host/` | 电脑上跑的单元测试（Unity），`fakes/` 是假的 CAN / SPI / PWM / 时钟 / OS |
| `tests/target/`、`tests/hil/` | 规划中（板上测试、硬件在环），只有 README |
| `tools/heater_model.py` | IMU 加热热模型：用实测数据拟合，在模型上比较加热参数 |
| `tools/keil_sync.py` | CubeMX 重新生成或增删源文件后，把 Keil 工程整理回能编译本框架的样子；`--check` 只检查 |
| `tools/gen_readme_diagrams.py` | 生成 README 里的结构图（`docs/images/*.svg`），改图改脚本，不手改 SVG |
| `tools/mujoco/` | MuJoCo 仿真：单电机 PID、哨兵半舵半全向底盘 |
| `docs/` | 架构设计、编码规范、开发环境、与旧工程的差异、调用关系、本文、待上板验证清单 |

---

## 0. 一张图

```
上电 → app_main（初始化、建任务）→ 调度器启动 → startup_task（允许解锁，删除自己）
                                                   ↓
   ┌──────────── 6 个任务同时运行，各自按周期 ────────────┐
   │ ins_task      1 ms   读 IMU → 算姿态 → 保存最新姿态    │
   │ comm_rx_task  有数据  解析遥控、电机反馈                 │
   │ control_task  1 ms   读输入 → 安全门 → 底盘 → 发电机指令 │
   │ detect_task   10 ms  打印设备上线 / 离线                │
   │ indicator_task 25 ms 灯、蜂鸣器、低电量                 │
   │ log_task      1 s    打印状态                          │
   └──────────────────────────────────────────────────────┘
中断：只把数据收下来、叫醒 comm_rx_task，不做解析
```

---

## 1. 上电

```
main()（CubeMX 生成）
    配置 MPU、开 Cache、初始化时钟和各外设
    调用 MX_FREERTOS_Init()
        调用 app_main()                                    → 01_applic/system/app_main.c:app_main
            初始化 DWT 计时器；失败就停住
            初始化 RTT 日志
            objects_init()：初始化这台车的全部对象              → 01_applic/config/objects.c:objects_init
                初始化 DR16 遥控（最新一帧保存在 dr16 里）
                对 4 个轮子电机：检查配置、查 ID 冲突、加入电机组
                初始化底盘（轮组类型、尺寸、PID 来自 params.h）
                初始化 ins（安装方向来自 params.h，最新姿态保存在 ins 里）
                初始化安全门（解锁拨杆 = 右拨杆），模式 = Init
                任何一步失败 → 停住，不建任何任务（电机不会收到指令）
            按任务表 task_table[] 逐个创建 6 个任务；有一个失败就停住
    启动调度器

startup_task（CubeMX 建的，优先级最高，第一个运行）     → 01_applic/system/app_main.c:startup_task
    安全门：系统就绪（之后才允许解锁）
    打印 "startup done"
    删除自己
```

---

## 2. 中断（只收数据）

```
串口空闲中断（DR16 一帧收完）                          → 05_platform/uart/uart_stm32h7.c:HAL_UARTEx_RxEventCallback
    数据已经由 DMA 写进循环缓冲，这里什么都不拷
    叫醒 comm_rx_task                                     → 01_applic/tasks/comm_rx_task.c:notify_from_isr

CAN 接收中断（收到一帧）                                → 05_platform/can/can_stm32h7.c:HAL_FDCAN_RxFifo0Callback
    把这一帧放进 CAN 接收环形缓冲（满了就丢弃并计数）
    叫醒 comm_rx_task
```

---

## 3. comm_rx_task：解析遥控和电机反馈（收到数据就运行）

```
任务开始：
    打开每一路 CAN 的接收
    打开 UART5（DR16）的 DMA 接收

永远循环：                                              → 01_applic/tasks/comm_rx_task.c:comm_rx_task_entry
    等待中断叫醒（最多等 10 ms，防止漏掉通知）

    对每一路 CAN：
        取出缓冲里的每一帧：
            依次问 4 个轮子电机“这帧是不是你的反馈”      → 02_devices/motor/motor.c:motor_receive
                总线对、反馈 ID 对（0x201–0x204）→ 是它的：
                    长度不是 8 → 丢弃
                    否则 解码出 角度、转速、力矩、温度（换成输出轴国际单位）
                         在临界区里 整份写进这个电机的反馈
                         记下“刚收到过数据”（在线检测用）
                    不再问后面的电机
    如果某路 CAN 进入 bus-off，并且距上次重启已过 100 ms → 重启它

    从 UART5 取出新收到的字节，交给 DR16 解析            → 02_devices/remote/dr16.c:dr16_on_bytes
        距上次收到数据超过 6 ms → 当作新的一帧开头
        每凑满 18 字节：
            检查摇杆值在 364–1684、拨杆值合法
            合法 → 减去中位 1024，保存为最新一帧（带时刻），记下“刚收到过数据”
            不合法 → 坏帧计数 +1
```

---

## 4. ins_task：姿态解算（每 1 ms）

```
任务开始：                                              → 01_applic/tasks/ins_task.c:ins_task_entry
    初始化 BMI088；失败就每 1 s 重试一次
    打印“开始标定，保持静止”

永远循环（每 1 ms）：
    ins_step()：                                         → 01_applic/modules/ins/ins.c:ins_step
        1. 读 BMI088（SPI2）
           读失败，或加速度几乎为 0（坏帧）→ 关加热，失败计数 +1，这一周期不更新
        2. 加热：每 1280 ms 用芯片温度算一次 PID，目标 40 °C
        3. 如果还在上电标定阶段：
               攒满 2000 个陀螺样本（2 s）
               抖动小、均值也小 → 把均值当零偏，进入运行阶段
               否则 → 报告原因（在动 / 零偏太大），重新攒
               标定完成前不保存姿态，ins_read 一直返回 false（安全门因此一直全车停）
        4. 运行阶段：
               陀螺、加速度从芯片坐标转到机体坐标（安装方向）
               静止时每 1 s 修正一点航向零偏
               加速度二阶低通
               四元数 EKF（用实测的时间间隔）
               算出 yaw / pitch / roll、多圈 yaw
        5. 保存为最新姿态（带时刻）
    把返回的事件（读失败、标定完成、标定被拒）写进日志
```

---

## 5. control_task：控制周期（每 1 ms）

```
永远循环（每 1 ms）：                                   → 01_applic/tasks/control_task.c:control_task_entry
    now = 当前时刻

    【第 1 步 读输入】
    rc  = dr16_read()：整份拷贝最新一帧；超过 200 ms 没更新 → 遥控丢失
    imu = ins_read()：整份拷贝最新姿态；超过 20 ms 没更新 → IMU 未就绪

    【第 2 步 安全门】                                   → 01_applic/system/safety_gate.c:safety_gate_update
    （见第 6 节）得到 stop_all（这一周期是否全车停）

    【第 3 步 底盘】
    如果 stop_all：目标速度 = 0
    否则：目标速度 = 摇杆换算（ch[3] 前后、ch[2] 左右、ch[0] 旋转，满杆速度见 params.h）
    底盘计算（见第 7 节），解锁后 300 ms 内输出限幅从 0 逐渐升到 1 倍

    【第 4 步 发送】
    如果 stop_all：把每个电机改写成它的停机动作（轮子 = 零力矩）
    电机组发送（见第 8 节）

    睡到下一个 1 ms
```

---

## 6. 安全门：能不能动

```
safety_gate_update(遥控, IMU 是否就绪, now)：           → 01_applic/system/safety_gate.c
    输入可用 = 遥控在线 并且 IMU 就绪
    拨杆在下 = 输入可用 并且 右拨杆在“下”

    按当前模式：
        Init（启动未完成）：
            系统就绪了 → 进入 Safe，清除“看到过拨杆在下”
        Safe（等待解锁）：
            输入不可用 → 清除“看到过拨杆在下”（掉线期间的拨杆位置不算）
            拨杆在下   → 记下“看到过拨杆在下”
            看到过拨杆在下，现在又拨到了中或上 → 解锁：进入 Manual，记下解锁时刻
        Manual（允许动作）：
            输入不可用 或 拨杆在下 → 回到 Safe（之后必须重新拨一次才能解锁）

    stop_all = 模式不是 Manual
```

结论：急停（拨杆在下）、遥控丢失、IMU 未就绪、还没解锁，任何一种都会全车停；恢复以后也要重新拨一次才能动。

---

## 7. 底盘：一次计算

```
chassis_step(目标速度, stop_all, 输出比例)：            → 01_applic/modules/chassis/chassis.c:chassis_step
    【读实测】
    对 4 个轮子：拷贝电机反馈；20 ms 内收到过反馈才算在线
    全部在线 → 用运动学正解算出底盘当前速度；否则当前速度记为 0

    如果 stop_all：
        目标速度 = 当前实测速度（解锁时不会突然跳变）
        清掉所有 PID 积分
        不写任何电机指令
        结束

    【算目标】
    有电机离线 → 目标改为 0（机构停：车受控减速）
    斜坡：平移加速度不超过 max_accel、旋转加速度不超过 max_alpha
    运动学逆解：底盘速度 → 每个轮子的目标转速（全向轮 / 麦轮 / 舵轮）

    【算输出】
    对每个轮子：
        在线 → 速度环 PID（目标转速 vs 实测转速）→ 力矩，按“PID 限幅 × 输出比例”限幅 → 记下这个电机本周期的力矩指令
        离线 → 清积分，不写指令（发送时会发零力矩）
```

---

## 8. 电机组发送：最终发什么

```
motor_group_send()：                                     → 02_devices/motor/motor_group.c:motor_group_send
    1. 确定每个电机最终发什么（final_output），按优先级：
           有停机动作 → 执行停机动作（零力矩 / 阻尼 / 失能）
           本周期没写力矩指令 → 零力矩
           反馈离线（20 ms 没收到） → 零力矩
           否则 → 发写入的力矩
    2. 编码 + 发送：
           DJI 电调：同一总线、同一控制帧的 4 个电调拼成一帧（0x200），每个占 2 字节电流值 → 放进 CAN 发送队列
                     （全部失能时只发一次 0，之后停发）
           达妙电机：每台一帧；需要使能 / 失能 / 清错时先发命令，否则发 MIT 帧
    3. 清空本周期的指令（下个周期不写就发零力矩）
```

---

## 9. 其他任务

```
detect_task（每 10 ms）：                                 → 01_applic/tasks/detect_task.c
    开始时打印一次“本固件有哪些设备”
    之后：设备在线 / 离线状态变了 → 打印一行
    （只报告，不决定停车；它和 dr16_read 等读取函数看的是同一个看门狗，判断结果一致）

indicator_task（每 25 ms）：                              → 01_applic/tasks/indicator_task.c
    开始时：打开 ADC、蜂鸣器，放启动音
    每次：
        状态灯：每秒绿灯闪两下（一长一短）
        电池：读电压；低于 21.0 V 持续 1 s → 低电量（回到 21.5 V 以上解除）；低电量期间每 2 s 响一次
        读安全门模式：刚进入 Manual → 解锁音；刚离开 Manual → 上锁音
        推进蜂鸣器的音符

log_task（每 1 s）：                                      → 01_applic/tasks/log_task.c
    打印：心跳计数和模式、遥控、4 个轮子（转速 / 目标 / 力矩 / 温度）、底盘目标、IMU 姿态和温度、电池电压
```

---

## 10. 共享数据：谁写、谁读

| 数据 | 谁写 | 谁读 | 怎么保证安全 |
| --- | --- | --- | --- |
| 遥控（`dr16` 对象里） | comm_rx_task（DR16 解析） | control_task、log_task（`dr16_read`） | 和接收时刻一起由看门狗保管，临界区里整份拷贝；200 ms 内才算在线（detect 日志同一判断） |
| 姿态（`ins` 对象里） | ins_task | control_task、log_task（`ins_read`） | 同上，20 ms 内才算就绪 |
| 电机反馈 | comm_rx_task（`motor_receive`） | control_task（底盘）、log_task | 临界区里整份拷贝；20 ms 内收到过才算在线 |
| 电机指令 | control_task（底盘） | control_task（`motor_group_send`） | 写和发在同一个任务里 |
| 安全门模式 | control_task | indicator_task、log_task | 单个值，读写是原子的；只用来提示和打印 |
| 电池电压 | indicator_task | log_task | 通过 `indicator_battery_v()` 读，只用来打印 |
