/**
 * @file    chassis.h
 * @brief   底盘子系统：目标底盘速度 → 斜坡 → 逆解 → 各轮闭环 → 电机力矩。支持四轮全向轮、麦轮、舵轮
 * @note    轮组由 ChassisConfig.type 选择，轮号顺序见 algorithm/kinematics/ 下对应的头文件。
 *          - 全向轮、麦轮：4 个驱动电机，各一个速度环；
 *          - 舵轮：另有 4 个转向电机，角度环（rad → rad/s）串速度环（rad/s → N·m）。
 *          control 任务每个周期调用一次 chassis_step()。停机的分工（运行时契约第 5 节）：
 *          - 全车停（安全门）：本子系统不写指令，只清积分、把斜坡起点对齐当前实测速度，
 *            发送出口把各电机改写成它的 stop_action（底盘为零力矩）；
 *          - 机构停（任一电机离线）：目标改为 0，其余在线的电机按斜坡受控减速到 0；离线的电机
 *            不写指令（电机组填零力矩）。全部恢复在线后，从当前斜坡值开始重新跟随目标。
 *          功率控制以后加在“各轮闭环之后、写力矩之前”（ADR 0022）。
 */
#pragma once

#include <stdbool.h>

#include "algorithm/control/pid.h"
#include "algorithm/kinematics/chassis_vel.h"
#include "algorithm/kinematics/mecanum.h"
#include "algorithm/kinematics/omni.h"
#include "algorithm/kinematics/steer.h"
#include "devices/motor/motor.h"
#include "platform/compiler.h"

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
    CHASSIS_OMNI,
    CHASSIS_MECANUM,
    CHASSIS_STEER,
} ChassisType;

/** 舵轮专用 */
typedef struct
{
    SteerConfig kinematics;
    /* 转向电机反馈角度（输出轴 rad）为这个值时，轮子朝向底盘 X 轴（heading = 0）。
     * 转向电机的角度零点必须上电即确定（如 GM6020 的绝对编码器），上电位置为 0 的型号不能直接用 */
    float zero_rad[CHASSIS_WHEELS];
    PidParam angle_pid; /* 朝向误差 rad → 转向电机目标转速 rad/s */
    PidParam speed_pid; /* 转向电机转速 rad/s → N·m */
} ChassisSteerConfig;

typedef struct
{
    ChassisType type;
    OmniConfig omni;          /* 只有 CHASSIS_OMNI 使用 */
    MecanumConfig mecanum;    /* 只有 CHASSIS_MECANUM 使用 */
    ChassisSteerConfig steer; /* 只有 CHASSIS_STEER 使用 */
    PidParam
        drive_speed_pid; /* 驱动轮速度环：输出轴 rad/s → N·m；PID 不带 dt，按 1 kHz 整定（ADR 0029） */
    float max_accel_m_s2; /* 目标平移速度的最大变化率（合成大小；加速、减速和机构停都用它） */
    float max_alpha_rad_s2; /* 目标旋转速度的最大变化率 */
} ChassisConfig;

typedef struct
{
    const ChassisConfig *cfg;
    Motor *drive[CHASSIS_WHEELS];
    Motor *steer[CHASSIS_WHEELS]; /* 只有舵轮使用 */
    Omni omni;                    /* 只有全向轮使用 */
    Pid drive_pid[CHASSIS_WHEELS];
    Pid steer_angle_pid[CHASSIS_WHEELS];
    Pid steer_speed_pid[CHASSIS_WHEELS];
    /* 以下只在 control 任务里写；保留在结构体里方便在 Ozone 里看 */
    ChassisVel ref;                        /* 斜坡后的目标（底盘系） */
    float drive_ref_rad_s[CHASSIS_WHEELS]; /* 各驱动轮目标转速 */
    float heading_ref_rad[CHASSIS_WHEELS]; /* 舵轮：各轮目标朝向 */
    bool all_online;                       /* 本周期全部电机是否在线 */
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
 * @param   target        目标底盘速度（底盘系）；stop_all 时不读
 * @param   stop_all      安全门本周期要求全车停
 * @param   output_scale  输出限幅比例 0–1（safety_gate_output_scale()，解锁后逐渐放开）
 * @param   dt_s          控制周期，用于斜坡
 */
void chassis_step(Chassis *chassis, const ChassisVel *target, bool stop_all, float output_scale,
                  float dt_s);

#ifdef __cplusplus
}
#endif
