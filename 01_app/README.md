# 01_app/

业务层，相当于老模板的 `Application/`。分两类目录，平铺在这一层：

| 目录 | 类别 | 内容 |
| --- | --- | --- |
| `common/` | 各兵种共用 | `comm_rx_task.c`（收 CAN 帧、串口和 USB 字节，交给设备解析）、`daemon_task.c`（设备上线 / 离线报告、CAN bus-off 恢复）、`safety_gate.c`（安全门与模式，全车停） |
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
├── ins_task.c        1 kHz：姿态解算
└── heartbeat_task.c  25 ms：状态灯、蜂鸣器、电池、每秒 RTT 打印
```

- 读一个兵种：先看 `robot.c`（从上往下就是上电顺序和任务表），再看各 `*_task.c`。调用关系总图见 `docs/CALL_FLOW.md`。
- 兵种目录内的对象定义在 `robot.c`、声明在 `robot.h`，只给本目录的文件用（相当于老模板的全局变量，ADR 0044）。
- 新兵种：复制 `infantry/`，在 `CMakePresets.json` 里加一个预设（`RM_ROBOT` = 目录名）。

## 规则

- 机构目录（`chassis/`、`ins/`）：一个机构的完整闭环，只用 devices、algorithm、core；机构之间不互相 include，只走话题。电脑上可测。
- 兵种目录：选模块、填参数、模式状态机、创建任务；**禁止**写控制算法、直接操作外设。
- `common/safety_gate` 和各机构是纯逻辑（库 `rm_app_logic`，电脑测试和固件都链接）；任务文件只用于固件（库 `rm_app`）。
