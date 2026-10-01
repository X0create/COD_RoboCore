# heavy/：兵种：重装

**状态：规划，尚无代码。**

- 做什么：**规则未出**（《架构设计》“未决”：重装机器人规则细节待提供），机构组成待定。
- 计划的文件：`heavy_config.h`、`heavy_robot.h/.c`、`heavy_control_task.c`、`heavy_comm_rx_task.c`、`heavy_log_task.c`
- 参考：复制 `01_applic/robots/infantry/` 起步
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-heavy-debug`，并运行 `tools/keil_sync.py` 加 Keil Target。
