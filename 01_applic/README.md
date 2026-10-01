# 01_applic/

业务层，相当于老模板的 `Application/`。分四类子目录（ADR 0054、0055）：

```
01_applic/
├── system/      通用框架：上电顺序、安全门、接收公共部分
├── tasks/       全部 6 个任务（≈ 老模板 Application/Task）
├── modules/     机构，可复用（只放计算，不放任务）
│   ├── chassis/  ins/
│   └── gimbal/  shooter/  leg/  arm/          （规划）
└── robot/       这台车的参数、对象、任务表（≈ 老模板 Config.h + 全局变量）
```

| 目录 | 内容 |
| --- | --- |
| `system/` | `app_main.c`（上电顺序，只有这一份）、`safety_gate.c`（全车唯一的安全门） |
| `tasks/` | 全部任务，每个一对 `xxx_task.h/.c`（入口声明在 .h）：`ins_task.c`（1 kHz 姿态）、`control_task.c`（1 kHz 控制）、`comm_rx_task.c`（接收的完整流程：中断唤醒、打开接收、分派、bus-off 恢复）、`detect_task.c`（10 ms 上线 / 离线）、`indicator_task.c`（25 ms 灯、蜂鸣器、电池）、`log_task.c`（1 s 打印） |
| `modules/chassis/` | 底盘：按 `ChassisConfig.type` 选全向轮 / 麦轮 / 舵轮，读实测 → 算目标 → 算输出（ADR 0043） |
| `modules/ins/` | 惯性导航：`ins.c`（BMI088 → 零偏标定 → EKF → 保存最新姿态，`ins_read()` 读） |
| `modules/gimbal/`、`shooter/`、`leg/`、`arm/` | （规划）云台、发射、轮腿、机械臂（工程） |
| `robot/` | 这台车（预设 `h723-debug`）。目前：四轮全向轮底盘，遥控直接给底盘速度 |

## robot/：这台车的参数和对象

```
robot/
├── robot_config.h          全部参数：电机表、底盘尺寸与 PID、满杆速度、解锁拨杆、IMU 安装方向
├── robot.h                 全部对象 + 各任务入口的声明
└── robot.c                 ① 对象定义 ② robot_init() ③ 任务表 robot_tasks[]（6 个任务的优先级、栈）
```

- 上电顺序通用，在 `system/app_main.c`：`app_main()` 调用 `robot_init()`，再按 `robot_tasks[]` 创建任务。
- 读这台车：先看 `robot.c`（对象和任务表），再看 `tasks/` 里的各个任务。调用关系总图见 `docs/CALL_FLOW.md`，中文伪代码见 `docs/LOGIC_PSEUDOCODE.md`。
- 对象定义在 `robot.c`、声明在 `robot.h`，只给本目录的文件用（相当于老模板的全局变量，ADR 0044）。
- **做一台具体的车（英雄、工程、哨兵……）：复制整个仓库，改 `robot/` 里的参数和对象、`tasks/control_task.c` 等任务里的控制逻辑**；需要新机构就在 `modules/` 里加（ADR 0056）。
  多板的车（如哨兵的云台板、底盘板）每块板一份仓库副本，用 `02_devices/board_link/` 交换数据。

## 一处定义

- **一个参数，一个定义位置**：位置见下表。
- **一个状态，一个权威来源**：安全门模式只在 `system/safety_gate.c` 的 `safety_gate`；电池状态只在 `tasks/indicator_task.c`（别处用 `indicator_battery_*()` 读）；
  遥控只在 dr16 对象（`dr16_read()`），姿态只在 ins 对象（`ins_read()`），电机反馈只在电机对象（`motor_read_feedback()`）。读的一方不另存副本。
- **任务放哪里**：全部任务都在 `tasks/`，`*_task.c` 不出现在别处（ADR 0057）。任务只负责“什么时候跑、按什么顺序调用谁”，计算在 `modules/` 等其他文件里。
- **一项职责，一个负责模块**：状态灯和蜂鸣器只有 indicator_task 操作；CAN 接收和 bus-off 恢复都在 comm_rx_task；上线 / 离线报告只在 detect_task；上电顺序只在 `app_main.c`。

## 参数在哪里

| 参数 | 位置 | 说明 |
| --- | --- | --- |
| 电机 ID、方向、总线、停机动作 | `robot/robot_config.h` 的电机表（如 `wheel_config`） | comm_rx_task 读每一路 CAN，总线不在别处重复 |
| 底盘尺寸、加速度、速度环 PID | `robot/robot_config.h` 的 `chassis_config` | PID 不带 dt，和 1 kHz 绑定（ADR 0029） |
| 满杆速度、解锁拨杆、IMU 安装方向 | `robot/robot_config.h` | |
| 串口接线（哪个串口接什么） | `tasks/comm_rx_task.c` 开头 | |
| 任务优先级、栈大小 | `robot/robot.c` 的任务表 | |
| 电池：分压比、低电量阈值 | `tasks/indicator_task.c` 的 `battery_config` | 板子和电池的属性，通用（ADR 0038） |
| M3508 / C620 换算常数 | `02_devices/motor/motor.h` 的 `DJI_M3508_*`、`DJI_C620_*` | 附录 A.2 |
| π | `03_algorithm/math/math_const.h` 的 `RM_PI` 等 | |
| EKF 噪声（Q、R）、加速度低通系数 | `modules/ins/ins.c` 开头 | 沿用旧工程 INS_Task.c |
| 陀螺零偏标定、静止时在线修正的阈值 | `modules/ins/ins.h` 的 `INS_CALIB_*`、`INS_STILL_*` | ADR 0033、0039 |
| IMU 加热：目标温度、周期、PID、占空比上限 | `02_devices/imu/bmi088.h` 的 `BMI088_HEATER_TARGET_C`、`bmi088.c` 的 `HEATER_*` | 按本板实测（ADR 0042） |
| 遥控丢失超时 200 ms | `02_devices/remote/dr16.h` 的 `RC_LOST_TIMEOUT_MS` | 安全约定（ADR 0030） |
| IMU 就绪判定 20 ms | `01_applic/modules/ins/ins.h` 的 `IMU_STALE_MS` | 安全约定（ADR 0034） |
| 电机离线超时 20 ms | `02_devices/motor/motor.h` 的 `MOTOR_OFFLINE_TIMEOUT_MS` | 安全约定（ADR 0031） |
| 解锁后输出斜坡 300 ms | `system/safety_gate.h` 的 `SAFETY_RAMP_MS` | |
| 达妙命令间隔 | `02_devices/motor/dm_motor.h` 的 `DM_CMD_INTERVAL_US` | |

## 规则

- 机构目录（`modules/` 下）：一个机构的完整闭环，只用 devices、algorithm、core；机构之间不互相 include，需要的数据由任务读出后传入。电脑上可测。
- `robot/`：选模块、填参数、创建任务；**禁止**写控制算法、直接操作外设。
- `system/safety_gate` 和各机构是纯逻辑（库 `rm_app_logic`，电脑测试和固件都链接）；任务文件和 `app_main.c` 只用于固件（库 `rm_app`）。
