# subsystems/

一个机构的完整闭环：`ins/`（姿态解算）、`gimbal/`、`chassis/`、`shooter/`、`leg/`。

- 每个周期先检查依赖的设备是否在线；有一个离线，就执行本机构规定的安全动作。
- 功率控制属于 chassis / leg 内部。
- **可以** include：devices、algorithm、core、msgs。
- **禁止** include 其他 subsystem：子系统之间只通过话题交换数据。
