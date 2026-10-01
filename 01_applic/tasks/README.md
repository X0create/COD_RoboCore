# tasks/：全部任务（≈ 老模板 Application/Task）

一个任务一个文件。任务只负责“什么时候跑、按什么顺序调用谁”，具体计算在其他文件里（ADR 0055、0057）。
每个任务由 `01_applic/config/task_table.c` 的任务表 `task_table[]` 创建（优先级、栈都写在那里）。

| 文件 | 优先级 | 周期 | 做什么 | 调用 |
| --- | --- | --- | --- | --- |
| `ins_task.c` | 6 | 1 ms | 初始化 BMI088（失败每 1 s 重试），然后每 1 ms 一次姿态解算，把事件写进日志 | `modules/ins/ins.c` |
| `comm_rx_task.c` | 5 | 收到数据 | 中断唤醒、打开接收；CAN 帧交给电机（`motor_receive`），UART5 字节交给 DR16；CAN bus-off 恢复（**接线写在这里**，接收的完整流程只在这一个文件） | `02_devices/motor`、`02_devices/remote/dr16` |
| `control_task.c` | 4 | 1 ms | 读输入 → 安全门 → 底盘 → 发送（**控制逻辑写在这里**） | `system/safety_gate`、`modules/chassis`、`02_devices/motor/motor_group` |
| `detect_task.c` | 3 | 10 ms | 上电打印设备清单，之后打印设备上线 / 离线（只报告，不决定停车） | `04_core/watchdog` |
| `indicator_task.c` | 2 | 25 ms | 状态灯一长一短、启动 / 解锁 / 上锁音、低电量每 2 s 响一次；打开 ADC 和蜂鸣器也在这里 | `02_devices/battery`、`02_devices/buzzer` |
| `log_task.c` | 1 | 1 s | 通过 RTT 打印模式、遥控、轮子、底盘目标、IMU、电池 | 只读 |
