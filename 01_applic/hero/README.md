# hero/：兵种：英雄

**状态：规划，尚无代码。**

- 做什么：底盘 + 云台 + 大弹丸发射。
- 计划的文件：`hero_config.h`、`hero_robot.h/.c`、`hero_control_task.c`、`hero_comm_rx_task.c`、`hero_log_task.c`
- 参考：复制 `01_applic/infantry/` 起步
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-hero-debug`，并运行 `tools/keil_sync.py` 加 Keil Target。
