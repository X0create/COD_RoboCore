# sentry/gimbal_board/：兵种：哨兵云台板（多板）

**状态：规划，尚无代码。**

- 做什么：双云台 + 发射；与 `../chassis_board/` 通过 `02_devices/board_link/` 交换话题，两块板共用一份 `board_link_table.c`。
- 计划的文件：`sentry_gimbal_config.h`、`sentry_gimbal_robot.h/.c`、`sentry_gimbal_control_task.c`、`sentry_gimbal_comm_rx_task.c`、`sentry_gimbal_log_task.c`
- 参考：ADR 0024（多板）
- 加代码时：文件名带兵种前缀，在 `CMakePresets.json` 加预设 `h723-sentry_gimbal-debug`（`RM_ROBOT` = `sentry/gimbal_board`），并运行 `tools/keil_sync.py` 加 Keil Target。
