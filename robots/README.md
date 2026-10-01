# robots/

**一个固件目标对应这里的一个目录。** 多板机器人每块板一个目录。

| 目录 | 内容 |
| --- | --- |
| `common/` | `app_main()` 与启动任务、`comm_rx` 任务（CAN 帧分发、串口字节交给设备解析）、`daemon` 任务（设备上线 / 离线报告）、安全门与模式（`safety_gate`，全车停） |
| `_template/` | 新兵种的样板，由 `tools/new_robot.py` 复制 |
| `infantry/` | 步兵（预设 `h723-infantry-debug`）。第一版只有底盘：四轮全向轮，遥控直接给底盘速度（ADR 0043）；文件同下一行 |
| `<兵种>/` | `config.h`（参数）、`objects.h`（全部对象的声明）、`robot.c`（对象定义、`robot_init()`、任务表）、`tasks.h`，**一个任务一个文件**：`control_task.c`（1 kHz 读输入 → 安全门 → 子系统 → 发送）、`ins_task.c`、`heartbeat_task.c`（状态灯、蜂鸣器、RTT 打印）。调用关系见 `docs/CALL_FLOW.md` |

- 负责：选模块、填参数、模式状态机、创建任务。
- 兵种目录内的对象定义在 `robot.c`、声明在 `objects.h`，只给本目录的任务文件用（相当于老模板的全局变量，ADR 0044）。
- **可以** include：所有下层。
- **禁止**：写控制算法、直接操作外设。
