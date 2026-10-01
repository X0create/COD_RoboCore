# sentry/chassis_board/：兵种：哨兵底盘板（多板）

**状态：规划，尚无代码。**

- 做什么：底盘 + 功率控制；与 `../gimbal_board/` 通过 `02_devices/board_link/` 交换话题。
- 计划的文件：`sentry_chassis_config.h`、`sentry_chassis_robot.h/.c`、`sentry_chassis_control_task.c`、`sentry_chassis_comm_rx_task.c`、`sentry_chassis_log_task.c`
- 参考：ADR 0024（多板）
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-sentry_chassis-debug`（`RM_ROBOT` = `sentry/chassis_board`），并运行 `tools/keil_sync.py` 加 Keil Target。
