# 应用配置

- `robot.h/.c`：原底盘配置与对象，默认预设不变。
- `motor_bench_robot.h`：`h723-m3508` 单电机参数及对象声明；对象在专用 `motor_bench_main.c` 定义。
  MC02 CAN1 仅一台 M3508/C620 ID 1（经典 CAN 1 Mbit/s，控制 0x200 / 反馈 0x201）；
  UART5 接 DR16，USB 参数 / 回读及记录。实物两端终端电阻及 ID 由用户核对。

本台架参数是等效单位的试验候选，默认 `motor_bench.settings.allow_motion=false`。
先核对方向、限流与温度工况并测停止 / 失联；完整操作见 `docs/M3508_CONTROL_BENCH.md`。
