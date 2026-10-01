# 调用关系地图（新旧模板对照）

更新时间：2026-09-30。对象以步兵 `01_app/infantry/` 为例；台架验证固件 `01_app/bench/` 的结构相同。

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
| `Core/`（CubeMX） | `06_boards/dm_mc02_h723/` | CubeMX 只建一个 `startup` 任务，其余任务由兵种 `robot.c` 的任务表创建（ADR 0025） |
| `BSP/bsp_*.c` | `05_platform/stm32h7/*.c`（接口在 `05_platform/*.h`） | 中断回调只收数据、唤醒 comm_rx 任务，不在中断里解析 |
| `Components/Algorithm/`、`Controller/` | `03_algorithm/`（`control/pid`、`filter/lpf`、`attitude/quat_ekf` …） | |
| `Components/Device/` | `02_devices/`（`motor/`、`remote/dr16`、`imu/bmi088` …） | |
| `Application/` | `01_app/` | 机构（`chassis/`、`ins/`）、各兵种共用（`common/`）、兵种（`infantry/`、`bench/`）平铺在这一层 |
| `Application/Task/INS_Task.c` | `01_app/<兵种>/ins_task.c` + `01_app/ins/ins.c` | |
| `Application/Task/Control_Task.c` | `01_app/<兵种>/control_task.c` + `01_app/chassis/chassis.c` | |
| `Application/Task/CAN_Task.c` | `control_task.c` 第 4 步 → `02_devices/motor/motor_group.c` | 发送与控制同一周期，不再单独一个任务 |
| `Application/Task/Detect_Task.c` | `01_app/common/detect_task.c` | 只报告上线 / 离线；是否停车由读数据的地方按时间戳当场判断 |
| `Core/Src/freertos.c` 的任务列表 | `01_app/<兵种>/robot.c` 第 3 节“任务表” | 5 个任务的优先级、栈都在这一处 |
| 全局变量 `remote_ctrl`、`Chassis_Motor[]` … | `01_app/<兵种>/robot.h`（定义在 `robot.c` 第 1 节） | 只在兵种目录内共享 |
| `Config.h` | `01_app/<兵种>/config.h` | |

## 3. 新模板的文件（步兵）

```
01_app/infantry/
├── config.h           参数：PID、底盘尺寸、摇杆满杆速度、解锁拨杆、IMU 安装方向、电池
├── robot.h            全部对象 + 各任务入口的声明                 ← 相当于老模板的全局变量
├── robot.c            从上往下：① 对象定义 ② init_objects() ③ 任务表 ④ app_main() ⑤ startup_task()
├── control_task.c     1 kHz：读输入 → 安全门 → 底盘 → 发送        ← Control_Task + CAN_Task
├── comm_rx_task.c     收到数据就运行：打开接收，CAN → 电机，UART5 → DR16 ← BSP 里的接收回调
├── ins_task.c         1 kHz：IMU 姿态解算                         ← INS_Task
└── heartbeat_task.c   25 ms：状态灯、蜂鸣器、电池、每秒 RTT 打印
01_app/common/（各兵种相同）
├── comm_rx.c          接收的公共部分：中断唤醒任务、分发 CAN、打开接收
├── detect_task.c      detect 任务：上线 / 离线报告、CAN bus-off 恢复 ← Detect_Task
└── safety_gate.c      安全门：急停、遥控丢失、未解锁、IMU 未就绪 → 全车停
01_app/chassis/chassis.c  底盘：读实测 → 算目标 → 算输出
01_app/ins/ins.c          惯性导航：读 BMI088 → 标定 → EKF → 发布 imu_state
```

## 4. 上电顺序（全部在 `01_app/infantry/robot.c`）

```
main()（CubeMX）→ MX_FREERTOS_Init()（freertos.c 的 USER CODE 区）
└─ app_main()                                   ④ 调度器启动前
   ├─ rm_time_init()、rm_log_init()
   ├─ init_objects()                            ② 遥控 → 4 个轮子电机 → 底盘 → IMU → 电池 → 安全门
   └─ create_tasks()                            ③ 按任务表创建 5 个任务（任何一步失败都停在 halt_on_init_failure）
（调度器启动）
startup_task()                                  ⑤ 最高优先级，第一个运行
├─ adc_start()、buzzer_init() + 启动音
├─ safety_gate_set_system_ready(&gate)          允许解锁
└─ rm_task_delete_self()
```

## 5. 每个任务做什么

### control 任务（1 kHz）：`01_app/infantry/control_task.c:control_task_entry`

```
for (;;)
├─ 1. 读输入
│  ├─ rc_state_read(&rc_state, ...)             04_core/msg/rc_state.h           200 ms 没更新 = 遥控丢失
│  └─ imu_state_read(&imu_state, ...)           04_core/msg/imu_state.h          20 ms 没更新 = IMU 未就绪
├─ 2. safety_gate_update(&gate, ...)            01_app/common/safety_gate.c   → stop_all（全车停）
├─ 3. chassis_cmd_from_rc(&rc)                  本文件                    摇杆 → 目标底盘速度
│  └─ chassis_step(&chassis, ...)               01_app/chassis/chassis.c
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

### ins 任务（1 kHz）：`01_app/infantry/ins_task.c:ins_task_entry`

```
ins_start(&ins)                                 01_app/ins/ins.c      初始化 BMI088（失败每 1 s 重试）
for (;;)
└─ ins_step(&ins)                               01_app/ins/ins.c
   ├─ bmi088_read()                             02_devices/imu/bmi088.c
   ├─ bmi088_heater_step()                      加热 PID
   ├─ （上电前 2 s）calibrate_gyro()            陀螺零偏标定，静止才采用
   └─ update_attitude()                         安装旋转 → 零偏在线修正 → 加速度低通 → quat_ekf_update → 欧拉角、多圈航向
      └─ imu_state_publish(&imu_state, ...)     control、heartbeat 读
```

### comm_rx 任务（收到数据就运行）：`01_app/infantry/comm_rx_task.c:comm_rx_task_entry`

```
中断：HAL_FDCAN_RxFifo0/1Callback               05_platform/stm32h7/can.c    帧放进环形缓冲
      HAL_UARTEx_RxEventCallback                05_platform/stm32h7/uart.c   DMA 收到的字节留在缓冲区
      └─ ⚡函数指针 notify → notify_from_isr()（01_app/common/comm_rx.c）  唤醒 comm_rx 任务
comm_rx 任务：
├─ comm_rx_start_can()、comm_rx_start_uart(UART_5)   任务开头打开接收（接线就写在这个文件里）
└─ for (;;)
   ├─ comm_rx_wait()                            等中断通知，最多 10 ms
   ├─ can_read(CAN_BUS_1) 取一帧                05_platform/stm32h7/can.c
   │  └─ motor_receive(&wheel_motor[i], …)      02_devices/motor/motor.c     总线和反馈 ID 对上就解码（dji / dm_decode_feedback）→ 存反馈、喂狗
   └─ uart_read(UART_5) → dr16_on_bytes()       02_devices/remote/dr16.c     凑满 18 字节 → dr16_decode → rc_state_publish
```

### detect 任务（10 ms）：`01_app/common/detect_task.c:detect_task_entry`

上电打印设备清单；之后 `watchdog_poll()` 打印上线 / 离线变化，`recover_bus_off()` 把 bus-off 的 CAN 拉回总线。只报告，不决定停车。

### heartbeat 任务（25 ms）：`01_app/infantry/heartbeat_task.c:heartbeat_task_entry`

状态灯（一长一短）、电池检查、解锁 / 上锁提示音、蜂鸣器；每 1 s 打印模式、遥控、四个轮子、底盘目标、IMU、电池。

## 6. 数据在任务之间怎么传

| 数据 | 写 | 读 | 方式 |
| --- | --- | --- | --- |
| 遥控 `rc_state` | comm_rx（dr16） | control、heartbeat | 话题：整份拷贝，带时间戳判断新旧 |
| 姿态 `imu_state` | ins | control、heartbeat | 话题 |
| 电机反馈 `wheel_motor[i].fb` | comm_rx（`motor_receive`） | control（`motor_read_feedback`） | 临界区拷贝，按时间戳判在线 |
| 电机指令 | control（`motor_set_torque`） | control（`motor_group_send`） | 同一任务内 |
| 安全门 `gate.mode`、底盘目标 | control | heartbeat（只打印） | 直接读，只供观察 |
