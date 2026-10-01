# 01_applic/

业务层，相当于老模板的 `Application/`。三类目录平铺在这一层：

| 目录 | 类别 | 内容 |
| --- | --- | --- |
| `system/` | 各兵种共用的框架部分 | `app_main.c`（上电顺序，只有这一份）、`safety_gate.c`（全车唯一的安全门）、`indicator_task.c`（状态灯、蜂鸣器、低电量）、`detect_task.c`（设备上线 / 离线报告）、`comm_rx_common.c`（接收的公共部分：中断唤醒、打开接收、CAN bus-off 恢复） |
| `chassis/` | 机构 | 底盘：按 `ChassisConfig.type` 选全向轮 / 麦轮 / 舵轮，读实测 → 算目标 → 算输出（ADR 0043） |
| `ins/` | 机构 | 惯性导航：`ins.c`（BMI088 → 零偏标定 → EKF → 发布 `imu_state`）、`ins_task.c`（1 kHz 任务，各兵种共用） |
| `gimbal/`、`shooter/`、`leg/`、`arm/` | 机构（规划） | 云台、发射、轮腿、机械臂（工程） |
| `infantry/` | 兵种 | 步兵（预设 `h723-infantry-debug`）。第一版只有底盘：四轮全向轮，遥控直接给底盘速度 |
| `hero/`、`engineer/`、`heavy/`、`wheel_leg/` | 兵种（规划） | 英雄、工程、重装（规则未出）、平衡步兵 |
| `sentry_gimbal/`、`sentry_chassis/` | 兵种（规划，多板） | 哨兵的云台板和底盘板，用 `02_devices/board_link/` 交换话题 |

## 兵种目录（以 `infantry/` 为例）

只放本兵种和别的兵种不同的东西：

```
infantry/
├── infantry_config.h           本兵种的全部参数：电机表、底盘尺寸与 PID、满杆速度、解锁拨杆、IMU 安装方向
├── infantry_robot.h            本兵种对象 + 本目录任务入口的声明
├── infantry_robot.c            ① 对象定义 ② robot_init() ③ 任务表 robot_tasks[]（6 个任务的优先级、栈）
├── infantry_control_task.c     1 kHz：读输入 → 安全门 → 底盘 → 发送
├── infantry_comm_rx_task.c     收到数据就运行：打开接收；CAN → 电机反馈，UART5 → DR16（接线写在这里）
└── infantry_log_task.c         1 s：通过 RTT 打印本兵种的状态
```

- 上电顺序各兵种相同，在 `system/app_main.c`：`app_main()` 调用本兵种的 `robot_init()`，再按 `robot_tasks[]` 创建任务。
- 读一个兵种：先看 `<兵种>_robot.c`（对象和任务表），再看各 `*_task.c`。调用关系总图见 `docs/CALL_FLOW.md`。
- 兵种目录内的对象定义在 `<兵种>_robot.c`、声明在 `<兵种>_robot.h`，只给本目录的文件用（相当于老模板的全局变量，ADR 0044）。
- 兵种目录里的文件名都带兵种前缀（`infantry_config.h`、`infantry_robot.c`），IDE 里同时打开几个兵种也分得清。
- 新兵种：复制 `infantry/`，文件名前缀改成新兵种名，在 `CMakePresets.json` 里加一个预设（`RM_ROBOT` = 目录名）。

## 一处定义

- **一个参数，一个定义位置**：位置见下表。
- **一个状态，一个权威来源**：安全门模式只在 `system/safety_gate.c` 的 `safety_gate`；电池状态只在 `indicator_task.c`（别处用 `indicator_battery_*()` 读）；
  遥控、姿态只在各自的话题；电机反馈只在电机对象（`motor_read_feedback()`）。读的一方不另存副本。
- **一项职责，一个负责模块**：状态灯和蜂鸣器只有 indicator_task 操作；CAN 接收和 bus-off 恢复都在 comm_rx_task；上线 / 离线报告只在 detect_task；上电顺序只在 `app_main.c`。

## 参数在哪里

| 参数 | 位置 | 说明 |
| --- | --- | --- |
| 电机 ID、方向、总线、停机动作 | `<兵种>/<兵种>_config.h` 的电机表（如 `wheel_config`） | comm_rx_task 读每一路 CAN，总线不在别处重复 |
| 底盘尺寸、加速度、速度环 PID | `<兵种>/<兵种>_config.h` 的 `chassis_config` | PID 不带 dt，和 1 kHz 绑定（ADR 0029） |
| 满杆速度、解锁拨杆、IMU 安装方向 | `<兵种>/<兵种>_config.h` | |
| 串口接线（哪个串口接什么） | `<兵种>/<兵种>_comm_rx_task.c` 开头 | |
| 任务优先级、栈大小 | `<兵种>/<兵种>_robot.c` 的任务表 | |
| 电池：分压比、低电量阈值 | `system/indicator_task.c` 的 `battery_config` | 板子和电池的属性，各兵种相同（ADR 0038） |
| M3508 / C620 换算常数 | `02_devices/motor/motor.h` 的 `DJI_M3508_*`、`DJI_C620_*` | 附录 A.2 |
| π | `03_algorithm/math/math_const.h` 的 `RM_PI` 等 | |
| EKF 噪声（Q、R）、加速度低通系数 | `ins/ins.c` 开头 | 沿用旧工程 INS_Task.c |
| 陀螺零偏标定、静止时在线修正的阈值 | `ins/ins.h` 的 `INS_CALIB_*`、`INS_STILL_*` | ADR 0033、0039 |
| IMU 加热：目标温度、周期、PID、占空比上限 | `02_devices/imu/bmi088.h` 的 `BMI088_HEATER_TARGET_C`、`bmi088.c` 的 `HEATER_*` | 按本板实测（ADR 0042） |
| 遥控丢失超时 200 ms | `04_core/msg/rc_state.h` 的 `RC_LOST_TIMEOUT_MS` | 安全约定（ADR 0030） |
| IMU 就绪判定 20 ms | `04_core/msg/imu_state.h` 的 `IMU_STALE_MS` | 安全约定（ADR 0034） |
| 电机离线超时 20 ms | `02_devices/motor/motor.h` 的 `MOTOR_OFFLINE_TIMEOUT_MS` | 安全约定（ADR 0031） |
| 解锁后输出斜坡 300 ms | `system/safety_gate.h` 的 `SAFETY_RAMP_MS` | |
| 达妙命令间隔 | `02_devices/motor/dm_motor.h` 的 `DM_CMD_INTERVAL_US` | |

## 规则

- 机构目录（`chassis/`、`ins/`）：一个机构的完整闭环，只用 devices、algorithm、core；机构之间不互相 include，只走话题。电脑上可测。
- 兵种目录：选模块、填参数、创建任务；**禁止**写控制算法、直接操作外设。
- `system/safety_gate` 和各机构是纯逻辑（库 `rm_app_logic`，电脑测试和固件都链接）；任务文件和 `app_main.c` 只用于固件（库 `rm_app`）。
