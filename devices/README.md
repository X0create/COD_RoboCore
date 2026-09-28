# devices/

每个外部设备的协议解析和状态：判断是否在线，并把原始值换算成国际单位。

| 目录 | 内容 |
| --- | --- |
| `motor/` | 统一电机接口、DJI 电机、达妙电机 |
| `imu/` | BMI088 |
| `remote/` | DR16、图传链路遥控 |
| `referee/` | 裁判系统 |
| `vision/` | 与上位机（自瞄）通信 |
| `supercap/` | 超级电容 |
| `board_link/` | 板间 CAN 通信 |

- 电机驱动只负责输出指令和解析反馈，PID 等闭环放在 subsystems。
- **可以** include：platform 接口、algorithm、core、msgs。
- **禁止** include：HAL、具体芯片头文件。
