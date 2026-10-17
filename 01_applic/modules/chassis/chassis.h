/**
 * @file    chassis.h
 * @brief   底盘子系统：四轮全向轮 / 麦轮 / 舵轮 / 半舵半全向，由 ChassisConfig.type 选择
 * @note    control_task 每 1 ms 按顺序调用下面三个函数，和 control_task 的三步一一对应：
 *            1. chassis_measure_update()：各电机的转速（舵轮还有朝向）→ chassis.measure
 *            2. chassis_target_update()：目标底盘速度过斜坡 → 逆解出各轮目标 → chassis.target
 *            3. chassis_output_update()：各轮 PID → 电机力矩
 *          在 Ozone 里看 chassis.measure 和 chassis.target 就能对比实测与目标。
 *
 *          全车停与降级（原则：赛场上优先保证车能继续跑，坏了一个轮子也不能整车趴窝）：
 *          - 全车停（安全门：急停、未解锁）：第 2 步传 stop_all = true，第 3 步就不写指令，
 *            发送出口统一改成零力矩；这时清积分、目标对齐实测速度，解锁时不会猛冲；
 *          - 降级（有轮子不能用）：不能用的轮子不写指令（发送时为零力矩，被动滚动）、清积分；
 *            其余轮子继续闭环，目标速度和加速度都乘 CHASSIS_DEGRADED_SCALE。
 *            “不能用”= 驱动电机离线 / 报错 / 未使能，或转向电机不可用 / 零点还没找到（朝向控制不了，驱动它会往错的方向推）。
 *          舵轮找零（接了零点开关时）：能动以后在第 3 步里先让转向电机正转、反转各经过开关一次，
 *          找到开关中心才开始正常控制；转向电机离线再上线后重找（上电位置为 0 的电机重连后零点不再可信）。
 *          轮号顺序见 03_algorithm/kinematics/ 下对应的头文件（都是左前起逆时针）。
 *          功率控制（按裁判系统给的功率上限压低力矩）以后加在第 3 步“算出力矩之后、写给电机之前”，
 *          算法在 03_algorithm/power/。
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
#include "05_platform/gpio/gpio.h"

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
    /* 零点开关（接近开关）。不接（GPIO_IN_NONE，默认）：转向电机零点必须上电即确定
     * （如 GM6020 的绝对编码器），zero_rad = 轮子朝车头时的反馈角度（输出轴 rad）。
     * 接了：上电位置为 0 的型号（M2006、M3508）也能用——能动以后先找开关中心，
     * zero_rad = 轮子朝车头时相对开关中心的偏移（由机械安装决定，通常只有几度，装好后实测） */
    GpioInput zero_switch[CHASSIS_WHEELS];
    float zero_rad[CHASSIS_WHEELS];
    bool zero_switch_active_low; /* 开关触发时为低电平（NPN 型接近开关一般是这样） */
    float zero_search_rad_s;     /* 找零时转向电机的转速（输出轴） */
    float zero_search_timeout_s; /* 超过这么久没找到就判失败，这个轮子不能用 */
    PidParam angle_pid;          /* 朝向误差 rad → 转向电机目标转速 rad/s */
    PidParam speed_pid;          /* 转向电机转速 rad/s → N·m */
} ChassisSteerConfig;

/** 底盘参数：写成 robot.h 里的常量 */
typedef struct
{
    ChassisType type;
    OmniConfig omni;          /* 只有全向轮使用 */
    MecanumConfig mecanum;    /* 只有麦轮使用 */
    ChassisSteerConfig steer; /* 舵轮、半舵半全向使用（转向电机零点和 PID） */
    HalfSteerConfig half_steer; /* 只有半舵半全向使用（几何和哪条对角线是舵轮） */
    PidParam drive_speed_pid; /* 驱动轮速度环：rad/s → N·m；PID 不带 dt，参数按 1 kHz 调用整定 */
    float max_accel_m_s2; /* 平移加速度上限（加速、减速都按它；降级时 × CHASSIS_DEGRADED_SCALE） */
    float max_alpha_rad_s2; /* 旋转角加速度上限 */
} ChassisConfig;

/** 实测（第 1 步写） */
typedef struct
{
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 有转向电机的轮子：各轮朝向 */
    float steer_angle_rad
        [CHASSIS_WHEELS]; /* 有转向电机的轮子：转向电机反馈角度（输出轴多圈，找零用） */
    float steer_speed_rad_s[CHASSIS_WHEELS]; /* 有转向电机的轮子：各转向电机转速 */
    bool drive_online[CHASSIS_WHEELS];
    bool steer_online[CHASSIS_WHEELS]; /* 没有转向电机的轮子恒为 false */
    bool steer_ready[CHASSIS_WHEELS]; /* 在线、无故障且已使能，才允许转向闭环和找零 */
    bool zero_switch_active[CHASSIS_WHEELS]; /* 零点开关此刻是否触发（没接开关的恒为 false） */
    bool wheel_ok
        [CHASSIS_WHEELS]; /* 轮子能用：驱动电机在线、无故障且使能；转向电机也须可用且零点已知 */
    bool all_online;      /* 全部轮子能用；有一个不能用就降级 */
    ChassisVel velocity;  /* 由各轮实测正解出的底盘速度（有电机离线时为 0） */
} ChassisMeasure;

/** 目标（第 2 步写） */
typedef struct
{
    ChassisVel velocity;                     /* 过斜坡之后的目标底盘速度 */
    float drive_speed_rad_s[CHASSIS_WHEELS]; /* 各驱动轮目标转速 */
    float heading_rad[CHASSIS_WHEELS];       /* 有转向电机的轮子：各轮目标朝向 */
    bool stop_all; /* 本周期全车停：第 3 步不写指令、只清积分 */
} ChassisTarget;

/**
 * 舵轮找零的状态：正转、反转各记一次“进入开关 → 离开开关”的中点，两个中点平均即开关中心。
 * 1 kHz 轮询开关，边沿记录的角度滞后电机 1 ms 转过的角度；正反两次的滞后方向相反，平均后抵消
 */
typedef enum
{
    STEER_ZERO_DONE,    /* 零点已知：找到了，或没接开关（零点上电即确定） */
    STEER_ZERO_FORWARD, /* 正转找开关 */
    STEER_ZERO_REVERSE, /* 反转再找一次 */
    STEER_ZERO_FAILED, /* 超时没找到：这个轮子不能用；转向电机离线再上线后重找 */
} SteerZeroState;

typedef struct
{
    SteerZeroState state;
    bool started;          /* 已经走过第一拍（第一拍只记电平） */
    bool was_active;       /* 上一拍开关是否触发 */
    bool seen_enter;       /* 当前方向已经看到“进入开关” */
    float enter_rad;       /* 当前方向进入开关时的角度 */
    float forward_mid_rad; /* 正转时进入、离开的中点 */
    float center_rad;      /* 开关中心（state 为 DONE 后有效；没接开关时为 0） */
    float elapsed_s;       /* 本次找零已用的时间（全车停时不计） */
} SteerZero;

typedef struct
{
    const ChassisConfig *cfg;
    Motor *drive[CHASSIS_WHEELS]; /* 驱动电机 */
    Motor *steer[CHASSIS_WHEELS]; /* 转向电机；没有转向电机的轮子为 NULL */
    Omni omni;                    /* 只有全向轮使用 */
    Pid drive_pid[CHASSIS_WHEELS];
    Pid steer_angle_pid[CHASSIS_WHEELS];
    Pid steer_speed_pid[CHASSIS_WHEELS];
    SteerZero zero[CHASSIS_WHEELS];              /* 有转向电机的轮子：找零状态 */
    bool drive_enable_requested[CHASSIS_WHEELS]; /* 本次解锁且在线期间是否请求过使能 */
    bool steer_enable_requested[CHASSIS_WHEELS];
    ChassisMeasure measure;
    ChassisTarget target;
} Chassis;

/**
 * @param   steer  各轮转向电机：舵轮 4 个都填；半舵半全向只填舵轮那两个、全向轮位置填 NULL；
 *                 其余轮组整个传 NULL
 * @return  false：所选轮组几何含非有限值或尺寸不大于 0；失败后不得调用三个更新函数
 * @pre     各电机已 motor_init()；cfg 在整个运行期间有效
 */
RM_NODISCARD bool chassis_init(Chassis *chassis, const ChassisConfig *cfg,
                               Motor *const drive[CHASSIS_WHEELS],
                               Motor *const steer[CHASSIS_WHEELS]);

/** 降级时目标速度和加速度的比例：少了轮子车会跑偏，降速给操作手留出修正的余量；数值是经验值，可按实车调整 */
#define CHASSIS_DEGRADED_SCALE 0.5f

/** 三个函数的调用周期：control_task 每 1 ms 调一次（斜坡按它算每步的速度变化；PID 参数也和它绑定） */
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
