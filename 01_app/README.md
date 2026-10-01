# 01_app/

业务层，相当于老模板的 `Application/`。分两类目录，平铺在这一层：

| 目录 | 类别 | 内容 |
| --- | --- | --- |
| `common/` | 各兵种共用 | `comm_rx.c`（接收的公共部分：中断唤醒任务、分发 CAN、打开接收）、`detect_task.c`（设备上线 / 离线报告、CAN bus-off 恢复）、`safety_gate.c`（安全门与模式，全车停） |
| `chassis/` | 机构 | 底盘：按 `ChassisConfig.type` 选全向轮 / 麦轮 / 舵轮，读实测 → 算目标 → 算输出（ADR 0043） |
| `ins/` | 机构 | 惯性导航：BMI088 → 零偏标定 → EKF → 发布 `imu_state` |
| `infantry/` | 兵种 | 步兵（预设 `h723-infantry-debug`）。第一版只有底盘：四轮全向轮，遥控直接给底盘速度 |
| `bench/` | 兵种 | 台架验证固件（预设 `h723-bench-debug`）：一台 M3508 速度环、一台达妙 DM8009、DR16、图传、USB 视觉链路，用于 `docs/VERIFICATION_TODO.md` 的逐项验证 |

## 兵种目录（以 `infantry/` 为例）

```
infantry/
├── config.h          全部参数：PID、底盘尺寸、满杆速度、解锁拨杆、IMU 安装方向、电池
├── robot.h           全部对象 + 各任务入口的声明
├── robot.c           ① 对象定义 ② init_objects() ③ 任务表（5 个任务的优先级、栈）
│                     ④ app_main()：调度器启动前 ⑤ startup_task()：打开接收、允许解锁
├── control_task.c    1 kHz：读输入 → 安全门 → 底盘 → 发送
├── comm_rx_task.c    收到数据就运行：打开接收；CAN → 电机反馈，UART5 → DR16（接线写在这里）
├── ins_task.c        1 kHz：姿态解算
└── heartbeat_task.c  25 ms：状态灯、蜂鸣器、电池、每秒 RTT 打印
```

- 读一个兵种：先看 `robot.c`（从上往下就是上电顺序和任务表），再看各 `*_task.c`。调用关系总图见 `docs/CALL_FLOW.md`。
- 兵种目录内的对象定义在 `robot.c`、声明在 `robot.h`，只给本目录的文件用（相当于老模板的全局变量，ADR 0044）。
- 新兵种：复制 `infantry/`，在 `CMakePresets.json` 里加一个预设（`RM_ROBOT` = 目录名）。

## 参数在哪里

兵种相关的参数都在 `<兵种>/config.h`；和兵种无关的（随传感器、板子或安全约定固定）留在各自模块里，所有兵种共用，改之前看对应 ADR。

| 参数 | 位置 | 说明 |
| --- | --- | --- |
| 电机 ID、方向、总线、停机动作 | `<兵种>/config.h` 的电机表（如 `wheel_config`） | 改总线时同步改 `<兵种>/comm_rx_task.c` 读哪路 CAN |
| 底盘尺寸、加速度、速度环 PID | `<兵种>/config.h` 的 `chassis_config` | PID 不带 dt，和 1 kHz 绑定（ADR 0029） |
| 满杆速度、解锁拨杆、电池阈值、IMU 安装方向 | `<兵种>/config.h` | |
| 串口接线（哪个串口接什么） | `<兵种>/comm_rx_task.c` 开头 | |
| 任务优先级、栈大小 | `<兵种>/robot.c` 的任务表 | |
| EKF 噪声（Q、R）、加速度低通系数 | `01_app/ins/ins.c` 开头 | 沿用旧工程 INS_Task.c |
| 陀螺零偏标定、静止时在线修正的阈值 | `01_app/ins/ins.h` 的 `INS_CALIB_*`、`INS_STILL_*` | ADR 0033、0039 |
| IMU 加热：目标温度、周期、PID、占空比上限 | `02_devices/imu/bmi088.h` 的 `BMI088_HEATER_TARGET_C`、`bmi088.c` 的 `HEATER_*` | 按本板实测（ADR 0042） |
| 遥控丢失超时 200 ms | `04_core/msg/rc_state.h` 的 `RC_LOST_TIMEOUT_MS` | 安全约定（ADR 0030） |
| IMU 就绪判定 20 ms | `04_core/msg/imu_state.h` 的 `IMU_STALE_MS` | 安全约定（ADR 0034） |
| 电机离线超时 20 ms | `02_devices/motor/motor.h` 的 `MOTOR_OFFLINE_TIMEOUT_MS` | 安全约定（ADR 0031） |
| 解锁后输出斜坡 300 ms | `01_app/common/safety_gate.h` 的 `SAFETY_RAMP_MS` | |
| 达妙命令间隔 | `02_devices/motor/dm_motor.h` 的 `DM_CMD_INTERVAL_US` | |

## 规则

- 机构目录（`chassis/`、`ins/`）：一个机构的完整闭环，只用 devices、algorithm、core；机构之间不互相 include，只走话题。电脑上可测。
- 兵种目录：选模块、填参数、模式状态机、创建任务；**禁止**写控制算法、直接操作外设。
- `common/safety_gate` 和各机构是纯逻辑（库 `rm_app_logic`，电脑测试和固件都链接）；任务文件只用于固件（库 `rm_app`）。
