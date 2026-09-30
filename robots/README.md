# robots/

**一个固件目标对应这里的一个目录。** 多板机器人每块板一个目录。

| 目录 | 内容 |
| --- | --- |
| `common/` | `app_main()` 与启动任务、`comm_rx` 任务（CAN 帧分发、串口字节交给设备解析）、`control` 任务（1 kHz 调用 `robot_control_step()`）、`daemon` 任务（设备上线 / 离线报告）、安全门与模式（`safety_gate`，全车停） |
| `_template/` | 新兵种的样板，由 `tools/new_robot.py` 复制 |
| `infantry/` | 步兵（预设 `h723-infantry-debug`）。第一版只有底盘：四轮全向轮，遥控直接给底盘速度（ADR 0043） |
| `<兵种>/` | `config.h`（PID 参数、解锁拨杆等固定参数）、`robot.c`（组装、话题实例、`robot_control_step()`）、`debug.c` |

- 负责：选模块、填参数、模式状态机、创建任务。
- **可以** include：所有下层。
- **禁止**：写控制算法、直接操作外设。
