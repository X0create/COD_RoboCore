# 调用关系地图（新旧模板对照）

更新时间：2026-09-30。对象以步兵 `robots/infantry/` 为例；样板 `robots/_template/` 的结构相同。

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
| `Core/`（CubeMX） | `boards/dm_mc02_h723/` | CubeMX 只建一个 `startup` 任务，其余任务由 `robot.c` 创建（ADR 0025） |
| `BSP/bsp_*.c` | `platform/stm32h7/*.c`（接口在 `platform/include/platform/`） | 中断回调只收数据、唤醒 comm_rx 任务，不在中断里解析 |
| `Components/Algorithm/`、`Controller/` | `algorithm/`（`control/pid`、`filter/lpf`、`attitude/quat_ekf` …） | |
| `Components/Device/` | `devices/`（`motor/`、`remote/dr16`、`imu/bmi088` …） | |
| `Application/Task/INS_Task.c` | `robots/<兵种>/ins_task.c` + `subsystems/ins/ins.c` | |
| `Application/Task/Control_Task.c` | `robots/<兵种>/control_task.c` + `subsystems/chassis/chassis.c` | |
| `Application/Task/CAN_Task.c` | `control_task.c` 第 4 步 → `devices/motor/motor_group.c` | 发送与控制同一周期，不再单独一个任务 |
| `Application/Task/Detect_Task.c` | `robots/common/daemon.c` | 只报告上线 / 离线；是否停车由读数据的地方按时间戳当场判断 |
| `Core/Src/freertos.c` 的任务列表 | `robots/<兵种>/robot.c` 最后的“任务表” | |
| 全局变量 `remote_ctrl`、`Chassis_Motor[]` … | `robots/<兵种>/objects.h`（定义在 `robot.c`） | 只在兵种目录内共享 |
| `Config.h` | `robots/<兵种>/config.h` | |

## 3. 新模板的文件（步兵）

```
robots/infantry/
├── config.h           参数：PID、底盘尺寸、摇杆满杆速度、解锁拨杆、IMU 安装方向、电池
├── objects.h          全部对象（话题、设备、子系统、安全门）的声明 ← 相当于老模板的全局变量
├── robot.c            对象定义、robot_init()、robot_start()、任务表
├── tasks.h            各任务入口函数的声明
├── control_task.c     1 kHz：读输入 → 安全门 → 底盘 → 发送        ← Control_Task + CAN_Task
├── ins_task.c         1 kHz：IMU 姿态解算                         ← INS_Task
└── heartbeat_task.c   25 ms：状态灯、蜂鸣器、电池、每秒 RTT 打印
robots/common/
├── app_main.c         上电顺序：app_main() → robot_init() → robot_create_tasks()；startup 任务 → 打开接收 → robot_start()
├── comm_rx.c          comm_rx 任务：收 CAN、串口、USB                ← BSP 里的接收回调
├── daemon.c           daemon 任务：上线 / 离线报告、CAN bus-off 恢复  ← Detect_Task
└── safety_gate.c      安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停
```

## 4. 上电顺序

```
main()（CubeMX）
└─ app_main()                                   robots/common/app_main.c
   ├─ rm_time_init()、rm_log_init()
   ├─ robot_init()                              robots/infantry/robot.c   设备 → 底盘 → IMU → 安全门
   └─ robot_create_tasks()                      robots/infantry/robot.c   按任务表创建 5 个任务
（调度器启动）
startup_task()                                  robots/common/app_main.c
├─ can_start(每路, comm_rx_notify_from_isr)      打开 CAN 接收
├─ comm_rx_start_uarts()、comm_rx_start_usb()    打开串口、USB 接收
├─ robot_start()                                robots/infantry/robot.c   允许解锁
└─ rm_task_delete_self()
```

## 5. 每个任务做什么

### control 任务（1 kHz）：`robots/infantry/control_task.c:control_task_entry`

```
for (;;)
├─ 1. 读输入
│  ├─ rc_state_read(&rc_state, ...)             msgs/rc_state.c           200 ms 没更新 = 遥控丢失
│  └─ imu_state_read(&imu_state, ...)           msgs/imu_state.c          20 ms 没更新 = IMU 未就绪
├─ 2. safety_gate_update(&gate, ...)            robots/common/safety_gate.c   → stop_all（全车停）
├─ 3. chassis_cmd_from_rc(&rc)                  本文件                    摇杆 → 目标底盘速度
│  └─ chassis_step(&chassis, ...)               subsystems/chassis/chassis.c
│     ├─ chassis_measure_update()               读 4 个电机实测（motor_read_feedback）→ 正解出底盘速度
│     ├─ （全车停）chassis_stop()               清积分，目标对齐实测，不写指令
│     ├─ chassis_target_update()                斜坡限加速度 → 逆解出每个轮子的目标转速（omni_inverse）
│     └─ chassis_output_update()                每轮速度环 pid_calc → motor_set_torque
└─ 4. 发送
   ├─ （全车停）motor_group_apply_stop_all()    devices/motor/motor_group.c   每个电机改写成它的停机动作
   └─ motor_group_flush(&motors)                devices/motor/motor_group.c   打包 0x200 / 0x1FF 帧 → can_send
```

和老模板一一对应：`chassis_measure_update` ↔ `Control_Measure_Update`，`chassis_target_update` ↔ `Control_Target_Update`，
`chassis_output_update` ↔ `Control_Info_Update`，`motor_group_flush` ↔ `CAN_Task` 里拆字节发送。

### ins 任务（1 kHz）：`robots/infantry/ins_task.c:ins_task_entry`

```
ins_start(&ins)                                 subsystems/ins/ins.c      初始化 BMI088（失败每 1 s 重试）
for (;;)
└─ ins_step(&ins)                               subsystems/ins/ins.c
   ├─ bmi088_read()                             devices/imu/bmi088.c
   ├─ bmi088_heater_step()                      加热 PID
   ├─ （上电前 2 s）calibrate_step()            陀螺零偏标定，静止才采用
   └─ run_step()                                安装旋转 → 零偏在线修正 → 加速度低通 → quat_ekf_update → 欧拉角、多圈航向
      └─ imu_state_publish(&imu_state, ...)     control、heartbeat 读
```

### comm_rx 任务（收到数据就运行）：`robots/common/comm_rx.c:comm_rx_entry`

```
中断：HAL_FDCAN_RxFifo0/1Callback               platform/stm32h7/can.c    帧放进环形缓冲
      HAL_UARTEx_RxEventCallback                platform/stm32h7/uart.c   DMA 收到的字节留在缓冲区
      └─ ⚡函数指针 notify → comm_rx_notify_from_isr()   唤醒 comm_rx 任务
comm_rx 任务：
├─ can_dispatch(每路)                           platform/stm32h7/can.c    按 CAN ID 找订阅者
│  └─ ⚡函数指针 → on_feedback()                 devices/motor/motor.c     解码（dji_decode_feedback / dm_decode_feedback）→ 存反馈、喂狗
│     （订阅在 motor_init() 里：每个电机订阅自己的反馈 ID，ID 冲突在初始化时报错）
└─ drain_uart(每个登记的串口)
   └─ ⚡函数指针 → on_dbus_bytes()               robots/infantry/robot.c
      └─ dr16_on_bytes(&dr16, ...)              devices/remote/dr16.c     凑满 18 字节 → dr16_decode → rc_state_publish
```

### daemon 任务（10 ms）：`robots/common/daemon.c:daemon_entry`

上电打印设备清单；之后 `watchdog_poll()` 打印上线 / 离线变化，`recover_bus_off()` 把 bus-off 的 CAN 拉回总线。只报告，不决定停车。

### heartbeat 任务（25 ms）：`robots/infantry/heartbeat_task.c:heartbeat_task_entry`

状态灯（一长一短）、电池检查、解锁 / 上锁提示音、蜂鸣器；每 1 s 打印模式、遥控、四个轮子、底盘目标、IMU、电池。

## 6. 数据在任务之间怎么传

| 数据 | 写 | 读 | 方式 |
| --- | --- | --- | --- |
| 遥控 `rc_state` | comm_rx（dr16） | control、heartbeat | 话题：整份拷贝，带时间戳判断新旧 |
| 姿态 `imu_state` | ins | control、heartbeat | 话题 |
| 电机反馈 `wheel_motor[i].fb` | comm_rx（on_feedback） | control（`motor_read_feedback`） | 临界区拷贝，按时间戳判在线 |
| 电机指令 | control（`motor_set_torque`） | control（`motor_group_flush`） | 同一任务内 |
| 安全门 `gate.mode`、底盘目标 | control | heartbeat（只打印） | 直接读，只供观察 |
