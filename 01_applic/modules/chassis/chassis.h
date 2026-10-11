/**
 * @file    chassis.h
 * @brief   底盘子系统：四轮全向轮 / 麦轮 / 舵轮 / 半舵半全向，由 ChassisConfig.type 选择
 * @note    control_task 每 1 ms 按顺序调用下面三个函数，各对应它自己的一步（与 COD-H7-Template Control_Task 的
 *          Measure / Target / Output 写法对应）：
 *            1. chassis_measure_update()：各电机的转速（舵轮还有朝向）→ chassis.measure
 *            2. chassis_target_update()：目标底盘速度过斜坡 → 逆解出各轮目标 → chassis.target
 *            3. chassis_output_update()：各轮 PID → 电机力矩
 *          在 Ozone 里看 chassis.measure 和 chassis.target 就能对比实测与目标。
 *
 *          两种停机（运行时契约第 5 节）：
 *          - 全车停（安全门：急停、遥控丢失、未解锁）：第 2 步传 stop_all = true，第 3 步就不写指令，
 *            发送出口统一改成零力矩；这时清积分、目标对齐实测速度，解锁时不会猛冲；
 *          - 机构停（任一电机离线）：目标改为 0，其余轮子按斜坡减速停下；离线的电机不写指令。
 *          轮号顺序见 03_algorithm/kinematics/ 下对应的头文件（都是左前起逆时针）。
 *          功率控制以后加在第 3 步“算出力矩之后、写给电机之前”（ADR 0022）。
 */
#pragma once

#include <stdbool.h>

#include "02_devices/motor/motor.h"
#include "03_algorithm/control/pid.h"
#include "03_algorithm/kinematics/chassis_vel.h"
#include "03_algorithm/kinematics/half_steer.h"
#include "03_algorithm/kinematics/mecanum.h"
#include "03_algorithm/kinematics/omni.h"
#include "03_algorithm/kinematics/steer.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 各种轮组都是 4 个轮子 */
#define CHASSIS_WHEELS 4u
_Static_assert(OMNI_WHEELS == CHASSIS_WHEELS && MECANUM_WHEELS == CHASSIS_WHEELS
                   && STEER_WHEELS == CHASSIS_WHEELS && HALF_STEER_WHEELS == CHASSIS_WHEELS,
               "各运动学的轮子数与底盘一致");

typedef enum
{
    CHASSIS_OMNI,       /* 全向轮 */
    CHASSIS_MECANUM,    /* 麦轮 */
    CHASSIS_STEER,      /* 舵轮：每个轮子多一个转向电机 */
    CHASSIS_HALF_STEER, /* 半舵半全向：一条对角线两个舵轮（各多一个转向电机），另一条两个全向轮 */
} ChassisType;

/** 转向电机的参数：舵轮和半舵半全向共用（kinematics 只有舵轮使用；半舵半全向只读舵轮那两个轮子的项） */
typedef struct
{
    SteerConfig kinematics;
    /* 转向电机反馈角度（输出轴 rad）为这个值时，轮子朝向车头（heading = 0）。
     * 转向电机的角度零点必须上电即确定（如 GM6020 的绝对编码器），上电位置为 0 的型号不能直接用 */
    float zero_rad[CHASSIS_WHEELS];
    PidParam angle_pid; /* 朝向误差 rad → 转向电机目标转速 rad/s */
    PidParam speed_pid; /* 转向电机转速 rad/s → N·m */
} ChassisSteerConfig;

/** 底盘参数：写成 robot.h 里的常量 */
typedef struct
{
    ChassisType type;
    OmniConfig omni;          /* 只有全向轮使用 */
    MecanumConfig mecanum;    /* 只有麦轮使用 */
    ChassisSteerConfig steer; /* 舵轮、半舵半全向使用（转向电机零点和 PID） */
    HalfSteerConfig half_steer; /* 只有半舵半全向使用（几何和哪条对角线是舵轮） */
    PidParam
        drive_speed_pid; /* 驱动轮速度环：rad/s → N·m；PID 不带 dt，按 1 kHz 整定（ADR 0029） */
    float max_accel_m_s2;   /* 平移加速度上限（加速、减速、机构停都按它） */
    float max_alpha_rad_s2; /* 旋转角加速度上限 */
} ChassisConfig;

/** 实测（第 1 步写） */
typedef struct
{
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 有转向电机的轮子：各轮朝向 */
    float steer_speed_rad_s[CHASSIS_WHEELS]; /* 有转向电机的轮子：各转向电机转速 */
    bool drive_online[CHASSIS_WHEELS];
    bool steer_online[CHASSIS_WHEELS]; /* 没有转向电机的轮子恒为 false，不参与 all_online */
    bool all_online;                   /* 全部电机在线；有一个离线就是机构停 */
    ChassisVel velocity; /* 由各轮实测正解出的底盘速度（有电机离线时为 0） */
} ChassisMeasure;

/** 目标（第 2 步写） */
typedef struct
{
    ChassisVel velocity;                     /* 过斜坡之后的目标底盘速度 */
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮目标转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 有转向电机的轮子：各轮目标朝向 */
    bool stop_all; /* 本周期全车停：第 3 步不写指令、只清积分 */
} ChassisTarget;

typedef struct
{
    const ChassisConfig *cfg;
    Motor *drive[CHASSIS_WHEELS]; /* 驱动电机 */
    Motor *steer[CHASSIS_WHEELS]; /* 转向电机；没有转向电机的轮子为 NULL */
    Omni omni;                    /* 只有全向轮使用 */
    Pid drive_pid[CHASSIS_WHEELS];
    Pid steer_angle_pid[CHASSIS_WHEELS];
    Pid steer_speed_pid[CHASSIS_WHEELS];
    ChassisMeasure measure;
    ChassisTarget target;
} Chassis;

/**
 * @param   steer  各轮转向电机：舵轮 4 个都填；半舵半全向只填舵轮那两个、全向轮位置填 NULL；
 *                 其余轮组整个传 NULL
 * @return  false：有电机不支持力矩指令（这种配置不能用在底盘上）
 * @pre     各电机已 motor_init()；cfg 在整个运行期间有效
 */
RM_NODISCARD bool chassis_init(Chassis *chassis, const ChassisConfig *cfg,
                               Motor *const drive[CHASSIS_WHEELS],
                               Motor *const steer[CHASSIS_WHEELS]);

/** 三个函数的调用周期：control_task 每 1 ms 调一次（斜坡按它算每步的速度变化；PID 参数也和它绑定，ADR 0029） */
#define CHASSIS_PERIOD_S 0.001f

/**
 * @brief   第 1 步 读实测：各电机反馈 → chassis.measure；全部在线时正解出实测底盘速度
 */
void chassis_measure_update(Chassis *chassis);

/**
 * @brief   第 2 步 算目标：目标底盘速度过斜坡 → 逆解出各轮目标 → chassis.target
 * @param   cmd       目标底盘速度（底盘系：vx 向前、vy 向左、wz 逆时针）；stop_all 时不读
 * @param   stop_all  全车停（安全门的结论）：目标对齐实测速度（解锁瞬间车还在滑行时，
 *                    目标从实测速度开始，不会突然跳到 0 或摇杆值），第 3 步不写指令
 * @pre     本周期已调用 chassis_measure_update()
 */
void chassis_target_update(Chassis *chassis, const ChassisVel *cmd, bool stop_all);

/**
 * @brief   第 3 步 算输出：每轮速度环（舵轮还有转向角度环 → 速度环）→ 写电机力矩；全车停时只清积分
 * @param   output_scale  输出限幅比例 0–1（safety_gate_output_scale()，解锁后逐渐放开）
 * @pre     本周期已调用 chassis_target_update()
 */
void chassis_output_update(Chassis *chassis, float output_scale);

#ifdef __cplusplus
}
#endif
