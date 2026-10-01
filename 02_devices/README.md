# 02_devices/

每个外部设备的协议解析和状态：判断是否在线，并把原始值换算成国际单位。

| 目录 | 内容 |
| --- | --- |
| `motor/` | 统一电机接口、DJI 电机、达妙电机（各型号共用一个类型，差异在配置里） |
| `imu/` | BMI088 |
| `remote/` | DR16、图传链路遥控 |
| `referee/` | 裁判系统 |
| `vision/` | 与上位机（自瞄）通信，默认走 USB 虚拟串口 |
| `supercap/` | 超级电容 |
| `battery/` | 电池电压（ADC），第一版只提示低电量 |
| `actuator/` | PWM 执行器：舵机（弹舱盖、工程机构）、气泵、电磁阀 |
| `buzzer/` | 蜂鸣器提示音，与状态灯闪烁码对应 |
| `board_link/` | 板间 CAN 通信 |

- 电机驱动只负责输出指令和解析反馈，PID 等闭环放在 app 的机构目录（如 `01_applic/modules/chassis`）。
- **可以** include：platform 接口、algorithm、core（如 `04_core/watchdog`）。
- **禁止** include：HAL、具体芯片头文件。
