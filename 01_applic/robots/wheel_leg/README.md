# wheel_leg/：兵种：平衡步兵（轮腿）

**状态：规划，尚无代码。**

- 做什么：轮腿 `leg` + 云台 + 发射。
- 计划的文件：`wheel_leg_config.h`、`wheel_leg_robot.h/.c`、`wheel_leg_control_task.c`、`wheel_leg_comm_rx_task.c`、`wheel_leg_log_task.c`
- 参考：复制 `01_applic/robots/infantry/` 起步
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-wheel_leg-debug`，并运行 `tools/keil_sync.py` 加 Keil Target。
