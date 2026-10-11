# 03_algorithm/

纯计算，输入输出全部通过参数传递，可以在 PC 上测试。库名 `rm_algorithm`，电脑测试和固件都链接它。
一个算法一对 `.h/.c`，文件名即算法名（ADR 0079）。

| 目录 | 内容 |
| --- | --- |
| `control/` | `pid`、`ramp`（斜坡）；（规划）LQR、前馈 |
| `filter/` | `lpf`（低通）、`kalman` |
| `math/` | `matrix`：小矩阵运算（卡尔曼等用，ADR 0029）；`math_const.h`：π 等常数 |
| `attitude/` | `quat_ekf`（四元数姿态 EKF）、`gyro_bias`（陀螺零偏标定）；（规划）云台角度 |
| `kinematics/` | 底盘运动学：全向轮 `omni`、麦轮 `mecanum`、舵轮 `steer`、半舵半全向 `half_steer`（共用底盘速度类型 `chassis_vel.h`）；轮腿 VMC 以后加 |
| `power/` | `power.h/.c`：电机功率模型与参数辨识、功率分配（ADR 0076）；`rls.h/.c`：通用的递推最小二乘（单独一对，ADR 0078） |
| `shoot/` | `shoot.h/.c`：热量限频、本地热量估算、卡弹检测与反转、开火判定、弹速自适应各一节（ADR 0077） |
| `ballistic/` | （规划）弹道解算 |

- **可以** include：C 标准库、CMSIS-DSP（可选）。
- **禁止**：RTOS、HAL、全局变量。
- 只返回状态，不打日志；由调用它的子系统记录错误并决定安全动作。
