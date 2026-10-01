# 调用关系地图（新旧模板对照）

更新时间：2026-09-30。对象以 `01_applic/config/`（目前是四轮全向轮底盘）为例。

只想先弄懂“做了什么”，看中文伪代码 `docs/LOGIC_PSEUDOCODE.md`；要看具体函数，再看本文。

读代码时先看本文，找到要看的那一步，再按 `文件:函数` 跳过去。
标 **⚡函数指针** 的地方，IDE 的“转到定义”跳不过去，按本文写的目标函数手动打开。

## 1. 老模板 COD-H7-Template 的结构

```
COD-H7-Template/
├── Core/                     CubeMX 生成：main.c、freertos.c（4 个任务在这里创建）、各外设初始化
├── BSP/                      板级封装，中断回调直接写在这里
│   ├── bsp_can.c             HAL_FDCAN_RxFifo0/1Callback → FDCANx_RxHandler → 直接更新 Chassis_Motor[]、DM_8009_Motor[]
│   ├── bsp_uart.c            HAL_UARTEx_RxEventCallback → USER_USART5_RxHandler → SBUS_TO_RC(&remote_ctrl)
│   └── bsp_spi / pwm / adc / gpio / dwt / tick / mcu.c
├── Components/
│   ├── Algorithm/            CRC、Kalman_Filter、LPF、Quaternion（EKF）、RLS、Ramp
│   ├── Controller/           PID
│   └── Device/               Bmi088、Motor（DJI + 达妙收发）、Remote_Control、Referee_System、
│                             Image_Transmission、MiniPC
├── Application/Task/         业务全在这里，一个任务一个文件
│   ├── INS_Task.c            1 kHz：读 BMI088 → 低通 → EKF → 欧拉角 / 圈数 → 每 5 ms 加热 PID
│   ├── Control_Task.c        1 kHz：Control_Measure_Update → Control_Target_Update → Control_Info_Update（PID）
│   ├── CAN_Task.c            1 kHz：把 Control_Info.SendValue[] 拆成字节填进 FDCAN1 帧发送；达妙使能、MIT 帧
│   ├── Detect_Task.c         1 kHz：Remote_Message_Moniter（遥控掉线检测）
│   └── Inc/Config.h          全局常量、IMU 轴序号、VAL_LIMIT 宏
└── USB_DEVICE/  SystemView/  MDK-ARM/（Keil 工程）
```

| 任务 | 优先级 | 栈 | 周期 |
| --- | --- | --- | --- |
| INS_Task | High | 2048 字 | osDelayUntil 1 ms |
| Control_Task | AboveNormal | 2048 字 | osDelay(1) |
| CAN_Task | Normal | 2048 字 | osDelay(1) |
| Detect_Task | BelowNormal | 2048 字 | osDelay(1) |

任务之间靠全局变量传数据：`remote_ctrl`、`Chassis_Motor[]`、`INS_Info`、`Control_Info`。

## 2. 老目录在新模板里的位置

| 老模板 | 新模板 | 说明 |
| --- | --- | --- |
| `Core/`（CubeMX） | `06_boards/dm_mc02_h723/` | CubeMX 只建一个 `startup` 任务，其余任务由 `config/task_table.c` 创建（ADR 0025） |
| `BSP/bsp_*.c` | `05_platform/stm32h7/*.c`（接口在 `05_platform/*.h`） | 中断回调只收数据、唤醒 comm_rx_task，不在中断里解析 |
| `Components/Algorithm/`、`Controller/` | `03_algorithm/`（`control/pid`、`filter/lpf`、`attitude/quat_ekf` …） | |
| `Components/Device/` | `02_devices/`（`motor/`、`remote/dr16`、`imu/bmi088` …） | |
| `Application/` | `01_applic/` | 通用（`system/`）、机构（`modules/chassis/`、`modules/ins/`）、这台车（`config/`） |
| `Application/Task/INS_Task.c` | `01_applic/tasks/ins_task.c` + `ins.c` | 通用 |
| `Application/Task/Control_Task.c` | `01_applic/tasks/control_task.c` + `01_applic/modules/chassis/chassis.c` | |
| `Application/Task/CAN_Task.c` | `control_task.c` 第 4 步 → `02_devices/motor/motor_group.c` | 发送与控制同一周期，不再单独一个任务 |
| `Application/Task/Detect_Task.c` | `01_applic/tasks/detect_task.c` | 只报告上线 / 离线；是否停车由读数据的地方按时间戳当场判断 |
| `Core/Src/freertos.c` 的任务列表 | `01_applic/config/task_table.c` 的 `task_table[]` | 6 个任务的优先级、栈都在这一处 |
| 全局变量 `remote_ctrl`、`Chassis_Motor[]` … | `01_applic/config/objects.h`（定义在 `objects.c` 第 1 节） | 只在 `config/` 内共享 |
| `Config.h` | `01_applic/config/params.h` | |

## 3. 新模板的文件

```
01_applic/tasks/（全部任务，≈ 老模板 Application/Task）
├── ins_task.c         1 kHz：姿态解算（调用 modules/ins/ins.c）       ← INS_Task
├── control_task.c     1 kHz：读输入 → 安全门 → 底盘 → 发送          ← Control_Task + CAN_Task
├── comm_rx_task.c     收到数据就运行：打开接收，CAN → 电机，UART5 → DR16 ← BSP 里的接收回调
├── detect_task.c      10 ms：上线 / 离线报告                        ← Detect_Task
├── indicator_task.c   25 ms：状态灯、蜂鸣器、低电量（照 UniC 的 app_indicator）
└── log_task.c         1 s：RTT 打印状态
01_applic/config/（这台车的参数、对象、任务表）
├── params.h           全部参数：电机表、PID、底盘尺寸、满杆速度、解锁拨杆、IMU 安装方向 ← Config.h
├── objects.h          全部对象的声明（任务入口在 tasks/ 各自的 .h）   ← 相当于老模板的全局变量
├── objects.c          对象定义 + objects_init()
└── task_table.c       任务表 task_table[]                             ← freertos.c 的任务列表
01_applic/system/（通用的框架）
├── app_main.c         上电顺序（只有这一份）：app_main()、startup_task()
└── safety_gate.c      全车唯一的安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停
01_applic/modules/ins/ins.c              惯性导航：一次姿态计算 ins_step()
01_applic/modules/chassis/chassis.c      底盘：读实测 → 算目标 → 算输出
```

## 4. 上电顺序（`01_applic/system/app_main.c`，通用）

```
main()（CubeMX）→ MX_FREERTOS_Init()（freertos.c 的 USER CODE 区）
└─ app_main()                                   调度器启动前
   ├─ rm_time_init()、rm_log_init()
   ├─ objects_init()                            01_applic/config/objects.c：遥控 → 4 个轮子电机 → 底盘 → IMU → 安全门
   └─ create_tasks()                            按 01_applic/config/task_table.c 的 task_table[] 创建 6 个任务
                                                （任何一步失败都停在 halt_on_init_failure）
（调度器启动）
startup_task()                                  最高优先级，第一个运行
├─ safety_gate_set_system_ready(&safety_gate)   允许解锁
└─ rm_task_delete_self()
各任务开头自己打开自己用的外设：comm_rx 打开 CAN / 串口，indicator 打开 ADC / 蜂鸣器，ins 初始化 BMI088
```

## 5. 每个任务做什么

### control_task（1 kHz）：`01_applic/tasks/control_task.c:control_task_entry`

```
for (;;)
├─ 1. 读输入
│  ├─ dr16_read(&dr16, &rc)                     02_devices/remote/dr16.c         200 ms 没更新 = 遥控丢失
│  └─ ins_read(&ins, &imu)                      01_applic/modules/ins/ins.c      20 ms 没更新 = IMU 未就绪
├─ 2. safety_gate_update(&safety_gate, ...)     01_applic/system/safety_gate.c   → stop_all（全车停）
├─ 3. chassis_cmd_from_rc(&rc)                  本文件                    摇杆 → 目标底盘速度
│  └─ chassis_step(&chassis, ...)               01_applic/modules/chassis/chassis.c
│     ├─ chassis_measure_update()               读 4 个电机实测（motor_read_feedback）→ 正解出底盘速度
│     ├─ （全车停）chassis_stop()               清积分，目标对齐实测，不写指令
│     ├─ chassis_target_update()                斜坡限加速度 → 逆解出每个轮子的目标转速（omni_inverse）
│     └─ chassis_output_update()                每轮速度环 pid_calc → motor_set_torque
└─ 4. 发送
   ├─ （全车停）motor_group_apply_stop_all()    02_devices/motor/motor_group.c   每个电机改写成它的停机动作
   └─ motor_group_send(&motors)                02_devices/motor/motor_group.c
      ├─ 1. 确定指令：final_output()             每个电机最终发什么：停机动作 > 没写指令 > 离线 > 力矩
      ├─ 2. 编码 → 3. 发送：send_dji_frame()    四台电调共用一帧（0x200 / 0x1FF）→ can_send
      │                     send_dm_frame()     达妙每台一帧：先对齐使能（清错 / 使能 / 失能命令），否则 MIT 帧
      └─ 4. 清理                               清空本周期指令：下个周期不写就发零力矩
```

和老模板一一对应：`chassis_measure_update` ↔ `Control_Measure_Update`，`chassis_target_update` ↔ `Control_Target_Update`，
`chassis_output_update` ↔ `Control_Info_Update`，`motor_group_send` ↔ `CAN_Task` 里拆字节发送。

### ins_task（1 kHz）：`01_applic/tasks/ins_task.c:ins_task_entry`

```
ins_start(&ins)                                 01_applic/modules/ins/ins.c      初始化 BMI088（失败每 1 s 重试）
for (;;)
└─ ins_step(&ins)                               01_applic/modules/ins/ins.c
   ├─ bmi088_read()                             02_devices/imu/bmi088.c
   ├─ bmi088_heater_step()                      加热 PID
   ├─ （上电前 2 s）calibrate_gyro()            陀螺零偏标定，静止才采用
   └─ update_attitude()                         安装旋转 → 零偏在线修正 → 加速度低通 → quat_ekf_update → 欧拉角、多圈航向
      └─ watchdog_feed_data(&ins->wd, ...)      保存最新姿态和时刻（ins_step 第 5 步），control、log 用 ins_read 读
```

### comm_rx_task（收到数据就运行）：`01_applic/tasks/comm_rx_task.c:comm_rx_task_entry`

```
中断：HAL_FDCAN_RxFifo0/1Callback               05_platform/can/can_stm32h7.c    帧放进环形缓冲
      HAL_UARTEx_RxEventCallback                05_platform/uart/uart_stm32h7.c   DMA 收到的字节留在缓冲区
      └─ ⚡函数指针 notify → notify_from_isr()（同一个 comm_rx_task.c）  唤醒 comm_rx_task
comm_rx_task：
├─ start_can()、start_uart(UART_5)            任务开头打开接收（接线就写在这个文件里）
└─ for (;;)
   ├─ rm_task_wait_notify(10)                   等中断通知，最多 10 ms
   ├─ 每一路 CAN：can_read() 取一帧            05_platform/can/can_stm32h7.c
   │  └─ motor_receive(&wheel_motor[i], …)      02_devices/motor/motor.c     总线和反馈 ID 对上就解码（dji / dm_decode_feedback）→ 存反馈、按中断里的接收时刻喂狗
   ├─ uart_read(UART_5) → dr16_on_bytes()       02_devices/remote/dr16.c     凑满 18 字节 → dr16_decode → watchdog_feed_data 保存到 dr16.rc
   └─ recover_bus_off()                         同一个文件                   bus-off 的总线每 100 ms 重启一次
```

### detect_task（10 ms）：`01_applic/tasks/detect_task.c:detect_task_entry`

上电打印设备清单；之后 `watchdog_poll()` 打印上线 / 离线变化。只报告，不决定停车。

### indicator_task（25 ms）：`01_applic/tasks/indicator_task.c:indicator_task_entry`

开头打开 ADC 和蜂鸣器、放启动音；之后每 25 ms：状态灯（一长一短）、电池检查（低电量每 2 s 响一次）、读 `safety_gate.mode` 放解锁 / 上锁音。

### log_task（1 s）：`01_applic/tasks/log_task.c:log_task_entry`

打印模式、遥控、四个轮子、底盘目标、IMU、电池（电池读 `indicator_battery_v()`）。

## 6. 数据在任务之间怎么传

| 数据 | 写 | 读 | 方式 |
| --- | --- | --- | --- |
| 遥控 `dr16.rc` | comm_rx（dr16） | control、log | `dr16_read()`：整份拷贝，带时间戳判断新旧 |
| 姿态 `ins.state` | ins | control、log | `ins_read()`，同上 |
| 电机反馈 `wheel_motor[i].fb` | comm_rx（`motor_receive`） | control（`motor_read_feedback`） | 临界区拷贝，按时间戳判在线 |
| 电机指令 | control（`motor_set_torque`） | control（`motor_group_send`） | 同一任务内 |
| 安全门 `safety_gate.mode` | control | indicator（提示音）、log（打印） | 直接读，单字节读写是原子的 |
| 底盘目标 | control | log（只打印） | 直接读，只供观察 |
| 电池电压、是否低电量 | indicator | log | `indicator_battery_v()` / `indicator_battery_low()` |
