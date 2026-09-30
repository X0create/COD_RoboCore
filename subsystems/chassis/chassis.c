/**
 * @file    chassis.c
 * @brief   底盘子系统，停机分工见 chassis.h
 */
#include "chassis.h"

#include <math.h>

#include "algorithm/control/ramp.h"

/** 本周期的实测：各电机反馈只读一次 */
typedef struct
{
    float drive_rad_s[CHASSIS_WHEELS];
    bool drive_online[CHASSIS_WHEELS];
    float heading_rad[CHASSIS_WHEELS]; /* 舵轮：各轮朝向 */
    float steer_rad_s[CHASSIS_WHEELS]; /* 舵轮：转向电机转速 */
    bool steer_online[CHASSIS_WHEELS];
} Measured;

bool chassis_init(Chassis *chassis, const ChassisConfig *cfg, Motor *const drive[CHASSIS_WHEELS],
                  Motor *const steer[CHASSIS_WHEELS])
{
    *chassis = (Chassis){ .cfg = cfg };
    if (cfg->type == CHASSIS_OMNI)
    {
        omni_init(&chassis->omni, &cfg->omni);
    }
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        if (!motor_supports_torque(drive[i]))
        {
            return false;
        }
        chassis->drive[i] = drive[i];
        pid_init(&chassis->drive_pid[i], PID_POSITION, &cfg->drive_speed_pid);
        if (cfg->type == CHASSIS_STEER)
        {
            if (!motor_supports_torque(steer[i]))
            {
                return false;
            }
            chassis->steer[i] = steer[i];
            pid_init(&chassis->steer_angle_pid[i], PID_POSITION, &cfg->steer.angle_pid);
            pid_init(&chassis->steer_speed_pid[i], PID_POSITION, &cfg->steer.speed_pid);
        }
    }
    return true;
}

static float clamp(float x, float limit)
{
    return (x > limit) ? limit : ((x < -limit) ? -limit : x);
}

/** 读全部反馈；返回是否全部在线 */
static bool measure(const Chassis *chassis, Measured *m)
{
    const bool steer = chassis->cfg->type == CHASSIS_STEER;
    bool all_online = true;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        MotorFeedback fb;
        m->drive_online[i] = motor_read_feedback(chassis->drive[i], &fb);
        m->drive_rad_s[i] = fb.speed_rad_s;
        all_online = all_online && m->drive_online[i];
        if (steer)
        {
            m->steer_online[i] = motor_read_feedback(chassis->steer[i], &fb);
            m->heading_rad[i] = fb.angle_rad - chassis->cfg->steer.zero_rad[i];
            m->steer_rad_s[i] = fb.speed_rad_s;
            all_online = all_online && m->steer_online[i];
        }
    }
    return all_online;
}

/** 由实测轮速（和朝向）正解出底盘速度 */
static void measured_velocity(const Chassis *chassis, const Measured *m, ChassisVel *vel)
{
    switch (chassis->cfg->type)
    {
        case CHASSIS_OMNI:
            omni_forward(&chassis->omni, m->drive_rad_s, vel);
            break;
        case CHASSIS_MECANUM:
            mecanum_forward(&chassis->cfg->mecanum, m->drive_rad_s, vel);
            break;
        case CHASSIS_STEER:
        {
            SteerWheel w[CHASSIS_WHEELS];
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                w[i] = (SteerWheel){ .heading_rad = m->heading_rad[i],
                                     .speed_rad_s = m->drive_rad_s[i] };
            }
            steer_forward(&chassis->cfg->steer.kinematics, w, vel);
            break;
        }
    }
}

/** 由斜坡后的目标逆解出各轮目标（舵轮还有朝向） */
static void wheel_targets(Chassis *chassis, const Measured *m)
{
    switch (chassis->cfg->type)
    {
        case CHASSIS_OMNI:
            omni_inverse(&chassis->omni, &chassis->ref, chassis->drive_ref_rad_s);
            break;
        case CHASSIS_MECANUM:
            mecanum_inverse(&chassis->cfg->mecanum, &chassis->ref, chassis->drive_ref_rad_s);
            break;
        case CHASSIS_STEER:
        {
            SteerWheel w[CHASSIS_WHEELS];
            steer_inverse(&chassis->cfg->steer.kinematics, &chassis->ref, m->heading_rad, w);
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                chassis->heading_ref_rad[i] = w[i].heading_rad;
                chassis->drive_ref_rad_s[i] = w[i].speed_rad_s;
            }
            break;
        }
    }
}

/*
 * 平移速度向目标移动，变化量的合成大小不超过 step。两个轴分别限幅的话，斜向加速时两轴同步增长，
 * 运动方向先偏到 45° 再转回来（舵轮会先转错朝向）
 */
static void ramp_translation(ChassisVel *ref, const ChassisVel *goal, float step)
{
    const float dx = goal->vx_m_s - ref->vx_m_s;
    const float dy = goal->vy_m_s - ref->vy_m_s;
    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= step)
    {
        ref->vx_m_s = goal->vx_m_s;
        ref->vy_m_s = goal->vy_m_s;
        return;
    }
    ref->vx_m_s += dx * (step / dist);
    ref->vy_m_s += dy * (step / dist);
}

static void reset_all_pid(Chassis *chassis)
{
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        pid_reset(&chassis->drive_pid[i]);
        pid_reset(&chassis->steer_angle_pid[i]);
        pid_reset(&chassis->steer_speed_pid[i]);
    }
}

void chassis_step(Chassis *chassis, const ChassisVel *target, bool stop_all, float output_scale,
                  float dt_s)
{
    const ChassisConfig *cfg = chassis->cfg;
    Measured m;
    chassis->all_online = measure(chassis, &m);

    if (stop_all)
    {
        /* 斜坡从当前实测速度开始：解锁时车还在滑行，目标也不会突然跳到 0 或摇杆值 */
        if (chassis->all_online)
        {
            measured_velocity(chassis, &m, &chassis->ref);
        }
        else
        {
            chassis->ref = (ChassisVel){ 0 };
        }
        reset_all_pid(chassis);
        return;
    }

    /* 机构停：有电机离线时目标改为 0，由斜坡受控减速 */
    const ChassisVel goal = chassis->all_online ? *target : (ChassisVel){ 0 };
    ramp_translation(&chassis->ref, &goal, cfg->max_accel_m_s2 * dt_s);
    chassis->ref.wz_rad_s =
        ramp_step(chassis->ref.wz_rad_s, goal.wz_rad_s, cfg->max_alpha_rad_s2 * dt_s);
    wheel_targets(chassis, &m);

    /* 各轮闭环。离线的电机不写指令：电机组填零力矩；清积分，恢复时不会猛冲 */
    const float drive_limit_nm = cfg->drive_speed_pid.output_limit * output_scale;
    const float steer_limit_nm = cfg->steer.speed_pid.output_limit * output_scale;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        if (m.drive_online[i])
        {
            const float torque_nm =
                pid_calc(&chassis->drive_pid[i], chassis->drive_ref_rad_s[i], m.drive_rad_s[i]);
            motor_set_torque(chassis->drive[i], clamp(torque_nm, drive_limit_nm));
        }
        else
        {
            pid_reset(&chassis->drive_pid[i]);
        }

        if (cfg->type != CHASSIS_STEER)
        {
            continue;
        }
        if (m.steer_online[i])
        {
            const float rate_rad_s = pid_calc(&chassis->steer_angle_pid[i],
                                              chassis->heading_ref_rad[i], m.heading_rad[i]);
            const float torque_nm =
                pid_calc(&chassis->steer_speed_pid[i], rate_rad_s, m.steer_rad_s[i]);
            motor_set_torque(chassis->steer[i], clamp(torque_nm, steer_limit_nm));
        }
        else
        {
            pid_reset(&chassis->steer_angle_pid[i]);
            pid_reset(&chassis->steer_speed_pid[i]);
        }
    }
}
