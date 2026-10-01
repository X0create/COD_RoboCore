/**
 * @file    chassis.h
 * @brief   底盘子系统：四轮全向轮 / 麦轮 / 舵轮，由 ChassisConfig.type 选择
 * @note    control 任务每 1 ms 调用一次 chassis_step()，分三步（与 COD-H7-Template Control_Task 的写法对应）：
 *            1. 读实测：各电机的转速（舵轮还有朝向）→ chassis.measure
 *            2. 算目标：目标底盘速度过斜坡 → 逆解出各轮目标 → chassis.target
 *            3. 算输出：各轮 PID → 电机力矩
 *          在 Ozone 里看 chassis.measure 和 chassis.target 就能对比实测与目标。
 *
 *          两种停机（运行时契约第 5 节）：
 *          - 全车停（安全门：急停、遥控丢失、未解锁）：不写指令，发送出口统一改成零力矩；
 *            这时清积分、目标对齐实测速度，解锁时不会猛冲；
 *          - 机构停（任一电机离线）：目标改为 0，其余轮子按斜坡减速停下；离线的电机不写指令。
 *          轮号顺序见 03_algorithm/kinematics/ 下对应的头文件（都是左前起逆时针）。
 *          功率控制以后加在第 3 步“算出力矩之后、写给电机之前”（ADR 0022）。
 */
#pragma once

#include <stdbool.h>

#include "02_devices/motor/motor.h"
#include "03_algorithm/control/pid.h"
#include "03_algorithm/kinematics/chassis_vel.h"
#include "03_algorithm/kinematics/mecanum.h"
#include "03_algorithm/kinematics/omni.h"
#include "03_algorithm/kinematics/steer.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 三种轮组都是 4 个轮子 */
#define CHASSIS_WHEELS 4u
_Static_assert(OMNI_WHEELS == CHASSIS_WHEELS && MECANUM_WHEELS == CHASSIS_WHEELS
                   && STEER_WHEELS == CHASSIS_WHEELS,
               "各运动学的轮子数与底盘一致");

typedef enum
{
    CHASSIS_OMNI,    /* 全向轮 */
    CHASSIS_MECANUM, /* 麦轮 */
    CHASSIS_STEER,   /* 舵轮：每个轮子多一个转向电机 */
} ChassisType;

/** 舵轮专用参数 */
typedef struct
{
    SteerConfig kinematics;
    /* 转向电机反馈角度（输出轴 rad）为这个值时，轮子朝向车头（heading = 0）。
     * 转向电机的角度零点必须上电即确定（如 GM6020 的绝对编码器），上电位置为 0 的型号不能直接用 */
    float zero_rad[CHASSIS_WHEELS];
    PidParam angle_pid; /* 朝向误差 rad → 转向电机目标转速 rad/s */
    PidParam speed_pid; /* 转向电机转速 rad/s → N·m */
} ChassisSteerConfig;

/** 底盘参数：写成兵种 config.h 里的常量 */
typedef struct
{
    ChassisType type;
    OmniConfig omni;          /* 只有全向轮使用 */
    MecanumConfig mecanum;    /* 只有麦轮使用 */
    ChassisSteerConfig steer; /* 只有舵轮使用 */
    PidParam
        drive_speed_pid; /* 驱动轮速度环：rad/s → N·m；PID 不带 dt，按 1 kHz 整定（ADR 0029） */
    float max_accel_m_s2;   /* 平移加速度上限（加速、减速、机构停都按它） */
    float max_alpha_rad_s2; /* 旋转角加速度上限 */
} ChassisConfig;

/** 实测（第 1 步写） */
typedef struct
{
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 舵轮：各轮朝向 */
    float steer_speed_rad_s[CHASSIS_WHEELS]; /* 舵轮：各转向电机转速 */
    bool drive_online[CHASSIS_WHEELS];
    bool steer_online[CHASSIS_WHEELS];
    bool all_online;     /* 全部电机在线；有一个离线就是机构停 */
    ChassisVel velocity; /* 由各轮实测正解出的底盘速度（有电机离线时为 0） */
} ChassisMeasure;

/** 目标（第 2 步写） */
typedef struct
{
    ChassisVel velocity;                     /* 过斜坡之后的目标底盘速度 */
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮目标转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 舵轮：各轮目标朝向 */
} ChassisTarget;

typedef struct
{
    const ChassisConfig *cfg;
    Motor *drive[CHASSIS_WHEELS]; /* 驱动电机 */
    Motor *steer[CHASSIS_WHEELS]; /* 转向电机，只有舵轮使用 */
    Omni omni;                    /* 只有全向轮使用 */
    Pid drive_pid[CHASSIS_WHEELS];
    Pid steer_angle_pid[CHASSIS_WHEELS];
    Pid steer_speed_pid[CHASSIS_WHEELS];
    ChassisMeasure measure;
    ChassisTarget target;
} Chassis;

/**
 * @param   steer  各轮转向电机，只有舵轮使用，其余轮组传 NULL
 * @return  false：有电机不支持力矩指令（这种配置不能用在底盘上）
 * @pre     各电机已 motor_init()；cfg 在整个运行期间有效
 */
RM_NODISCARD bool chassis_init(Chassis *chassis, const ChassisConfig *cfg,
                               Motor *const drive[CHASSIS_WHEELS],
                               Motor *const steer[CHASSIS_WHEELS]);

/**
 * @brief   每个控制周期调用一次
 * @param   cmd           目标底盘速度（底盘系：vx 向前、vy 向左、wz 逆时针）；stop_all 时不读
 * @param   stop_all      安全门本周期要求全车停
 * @param   output_scale  输出限幅比例 0–1（safety_gate_output_scale()，解锁后逐渐放开）
 * @param   dt_s          控制周期，用于斜坡
 */
void chassis_step(Chassis *chassis, const ChassisVel *cmd, bool stop_all, float output_scale,
                  float dt_s);

#ifdef __cplusplus
}
#endif
