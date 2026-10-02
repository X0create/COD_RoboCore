/**
 * @file    pid_bridge.h
 * @brief   单电机仿真用的 C PID 接口；仅用于电脑，不接实际电机
 * @note    一个 DLL 保存一个 PID 实例，只允许单线程调用。Python 不需要知道 Pid 的内存布局。
 */
#pragma once

/* Windows ctypes 通过导出符号找到这些函数，不能省略 dllexport。 */
#define PID_BRIDGE_API __declspec(dllexport)

/**
 * @brief 初始化位置式速度 PID。
 * @param kp 比例增益，N·m / (rad/s)。
 * @param ki 积分增益，每 1 ms 累加一次误差，不含 dt。
 * @param kd 微分增益，对相邻两次速度误差之差作用，不含 dt。
 * @param integral_limit 速度误差累加值的限幅。
 * @param output_limit_nm 输出轴力矩限幅，N·m。
 * @pre 参数有限，增益与限幅非负；单线程、固定 1 kHz 调用。
 */
PID_BRIDGE_API void pid_bridge_init(float kp, float ki, float kd, float integral_limit,
                                    float output_limit_nm);

/**
 * @brief 计算虚拟电机的力矩；未使能时清 PID 并返回零力矩。
 * @param target_rad_s 输出轴目标速度，rad/s。
 * @param measure_rad_s 输出轴实测速度，rad/s。
 * @param enabled 非零表示使能；这里只是仿真开关，不代表实车的安全门。
 * @return 输出轴力矩，N·m。
 * @pre 已初始化，输入有限；只能每 1 ms 调用一次。
 */
PID_BRIDGE_API float pid_bridge_step(float target_rad_s, float measure_rad_s, int enabled);
