/**
 * @file    chassis_bridge.h
 * @brief   半舵半全向 MuJoCo 适配；只在电脑上单线程调用，绝不连接实际 CAN
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ctypes 需要 Windows DLL 导出符号；该目录只构建 Windows 64 位库。 */
#define CHASSIS_BRIDGE_API __declspec(dllexport)

typedef struct
{
    uint64_t now_us; /* 仿真时间，us */
    float cmd[3];    /* 底盘系 vx、vy（m/s）、wz（rad/s） */
    float drive_angle_rad[4];
    float drive_speed_rad_s[4];
    float steer_angle_rad[4];
    float steer_speed_rad_s[4];
    uint32_t feedback_mask; /* 位 0–3：驱动轮反馈；位 4–7：对应轮的转向反馈 */
    uint32_t armed; /* 虚拟拨杆：0 = 下，非 0 = 中；仍由原安全门处理解锁边沿 */
    uint32_t rc_online; /* 虚拟遥控在线；不模拟 DR16 串口链路 */
    uint32_t imu_ready; /* 虚拟 IMU 就绪；不模拟 BMI088 / INS */
} ChassisBridgeInput;

typedef struct
{
    float drive_torque_nm[4]; /* 从最终发送的虚拟 CAN 帧解码得到，不直接取 PID 输出 */
    float steer_torque_nm[4];
    float drive_target_rad_s[4];
    float heading_target_rad[4];
    float measure_velocity[3]; /* 电机运动学估计 vx、vy、wz */
    float target_velocity[3];  /* 斜坡后的 vx、vy、wz */
    uint32_t mode;             /* 原 SafetyMode */
    uint32_t all_online;
} ChassisBridgeOutput;

/** 用于检查 Python ctypes 字段布局，避免结构体改动后传错内存。 */
CHASSIS_BRIDGE_API size_t chassis_bridge_input_size(void);
CHASSIS_BRIDGE_API size_t chassis_bridge_output_size(void);
/** 固定控制周期，us；Python 同时用它设置物理仿真步长。 */
CHASSIS_BRIDGE_API uint32_t chassis_bridge_period_us(void);
/**
 * @brief 初始化虚拟电机、安全门、原有 Chassis；每次加载 DLL 只调用一次。
 * @param radius_m 所有轮的半径，m；几何来源为 Python 模型。
 * @param half_wheelbase_m 前后接地点距离的一半，m。
 * @param half_track_m 左右接地点距离的一半，m。
 * @param diagonal 0：左前 / 右后舵轮；1：左后 / 右前舵轮。
 * @return 配置有效且初始化成功。
 * @note 电机协议全部用虚拟 M3508，供复用接口；不确定实车转向型号或零点方案。
 */
CHASSIS_BRIDGE_API bool chassis_bridge_init(float radius_m, float half_wheelbase_m,
                                            float half_track_m, int diagonal);
/** @pre 已初始化；固定 1 kHz；输入有限，角速度绝对值 < 100 rad/s，时间单调递增。 */
CHASSIS_BRIDGE_API void chassis_bridge_step(const ChassisBridgeInput *in, ChassisBridgeOutput *out);
