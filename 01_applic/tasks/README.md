# tasks/：通用的任务

任务只负责“什么时候跑、按什么顺序调用谁”，具体计算在其他文件里（ADR 0055）。这台车的任务（control、comm_rx、log）在 `robot/`。
每个任务由 `01_applic/robot/robot.c` 的任务表 `robot_tasks[]` 创建（优先级、栈都写在那里）。

| 文件 | 周期 | 做什么 | 调用 |
| --- | --- | --- | --- |
| `ins_task.c` | 1 ms | 初始化 BMI088（失败每 1 s 重试），然后每 1 ms 一次姿态解算，把事件写进日志 | `modules/ins/ins.c` 的 `ins_start()`、`ins_step()` |
| `detect_task.c` | 10 ms | 上电打印设备清单，之后打印设备上线 / 离线（只报告，不决定停车） | `04_core/watchdog` |
| `indicator_task.c` | 25 ms | 状态灯一长一短、启动 / 解锁 / 上锁音、低电量每 2 s 响一次；打开 ADC 和蜂鸣器也在这里 | `02_devices/battery`、`02_devices/buzzer`，读 `system/safety_gate` 的模式 |
