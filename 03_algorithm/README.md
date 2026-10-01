# 03_algorithm/

纯计算，输入输出全部通过参数传递，可以在 PC 上测试。库名 `rm_algorithm`，电脑测试和固件都链接它。

| 目录 | 内容 |
| --- | --- |
| `control/` | PID、斜坡、LQR、前馈 |
| `filter/` | 低通、卡尔曼 |
| `math/` | 小矩阵运算（卡尔曼等用，ADR 0029） |
| `attitude/` | 四元数、姿态 EKF、云台角度、陀螺零偏标定 |
| `kinematics/` | 底盘运动学：全向轮 `omni`、麦轮 `mecanum`、舵轮 `steer`、半舵半全向 `half_steer`（共用底盘速度类型 `chassis_vel.h`）；轮腿 VMC 以后加 |
| `power/` | 电机功率模型、参数辨识、功率分配 |
| `ballistic/` | 弹道解算 |

- **可以** include：C 标准库、CMSIS-DSP（可选）。
- **禁止**：RTOS、HAL、全局变量。
- 只返回状态，不打日志；由调用它的子系统记录错误并决定安全动作。
