# engineer/：兵种：工程

**状态：规划，尚无代码。**

- 做什么：底盘 + 机械臂 + 自定义控制器。
- 计划的文件：`engineer_config.h`、`engineer_robot.h/.c`、`engineer_control_task.c`、`engineer_comm_rx_task.c`、`engineer_log_task.c`
- 参考：复制 `01_applic/infantry/` 起步
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-engineer-debug`，并运行 `tools/keil_sync.py` 加 Keil Target。
