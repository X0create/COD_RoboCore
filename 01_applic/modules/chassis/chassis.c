/**
 * @file    chassis.c
 * @brief   底盘子系统：读实测 → 算目标 → 算输出（说明见 chassis.h）
 */
#include "chassis.h"

#include <math.h>

#include "03_algorithm/control/ramp.h"

static void chassis_measure_update(Chassis *chassis);
static void chassis_stop(Chassis *chassis);
static void chassis_target_update(Chassis *chassis, const ChassisVel *cmd, float dt_s);
static void chassis_output_update(Chassis *chassis, float output_scale);

/*
 * 记下配置和电机，按轮组类型初始化运动学和 PID。
 * 底盘靠力矩控制：有电机不支持力矩指令（如 GM6020 电压模式）就在初始化时拒绝，不等解锁后才发现不动
 */
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

void chassis_step(Chassis *chassis, const ChassisVel *cmd, bool stop_all, float output_scale,
                  float dt_s)
{
    /* 一个控制周期：读实测 → 全车停就收尾返回；否则 算目标 → 算输出（三步的函数都在下面，按顺序排列） */
    chassis_measure_update(chassis);
    if (stop_all)
    {
        chassis_stop(chassis);
        return;
    }
    chassis_target_update(chassis, cmd, dt_s);
    chassis_output_update(chassis, output_scale);
}

/* ------------------------------------------------------------------ */
/* 第 1 步：读实测                                                     */
/* ------------------------------------------------------------------ */

static void chassis_measure_update(Chassis *chassis)
{
    ChassisMeasure *m = &chassis->measure;
    const ChassisConfig *cfg = chassis->cfg;

    m->all_online = true;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        MotorFeedback fb;
        m->drive_online[i] = motor_read_feedback(chassis->drive[i], &fb);
        m->drive_speed_rad_s[i] = fb.speed_rad_s;
        m->all_online = m->all_online && m->drive_online[i];

        if (cfg->type == CHASSIS_STEER)
        {
            m->steer_online[i] = motor_read_feedback(chassis->steer[i], &fb);
            m->heading_rad[i] = fb.angle_rad - cfg->steer.zero_rad[i];
            m->steer_speed_rad_s[i] = fb.speed_rad_s;
            m->all_online = m->all_online && m->steer_online[i];
        }
    }

    /* 正解出实测底盘速度；有电机离线时各轮数据不全，记为 0 */
    if (!m->all_online)
    {
        m->velocity = (ChassisVel){ 0 };
        return;
    }
    switch (cfg->type)
    {
        case CHASSIS_OMNI:
            omni_forward(&chassis->omni, m->drive_speed_rad_s, &m->velocity);
            break;
        case CHASSIS_MECANUM:
            mecanum_forward(&cfg->mecanum, m->drive_speed_rad_s, &m->velocity);
            break;
        case CHASSIS_STEER:
        {
            SteerWheel wheel[CHASSIS_WHEELS];
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                wheel[i] = (SteerWheel){ .heading_rad = m->heading_rad[i],
                                         .speed_rad_s = m->drive_speed_rad_s[i] };
            }
            steer_forward(&cfg->steer.kinematics, wheel, &m->velocity);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 全车停：不写指令，清积分，目标对齐实测                               */
/* ------------------------------------------------------------------ */

static void chassis_stop(Chassis *chassis)
{
    /* 目标从当前实测速度开始：解锁时车还在滑行，目标也不会突然跳到 0 或摇杆值 */
    chassis->target.velocity = chassis->measure.velocity;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        pid_reset(&chassis->drive_pid[i]);
        pid_reset(&chassis->steer_angle_pid[i]);
        pid_reset(&chassis->steer_speed_pid[i]);
    }
}

/* ------------------------------------------------------------------ */
/* 第 2 步：算目标                                                     */
/* ------------------------------------------------------------------ */

/*
 * 平移速度向目标靠近，每次变化量的合成大小不超过 step。
 * 不用 x、y 分别限幅：那样斜着加速时两个方向同步增长，运动方向会先偏到 45°（舵轮会先转错朝向）
 */
static void ramp_translation(ChassisVel *v, const ChassisVel *goal, float step)
{
    const float dx = goal->vx_m_s - v->vx_m_s;
    const float dy = goal->vy_m_s - v->vy_m_s;
    const float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= step)
    {
        v->vx_m_s = goal->vx_m_s;
        v->vy_m_s = goal->vy_m_s;
        return;
    }
    v->vx_m_s += dx * (step / dist);
    v->vy_m_s += dy * (step / dist);
}

/* 目标速度 → 斜坡限加速度 → 运动学逆解，得到每个轮子的目标转速（舵轮还有目标朝向） */
static void chassis_target_update(Chassis *chassis, const ChassisVel *cmd, float dt_s)
{
    const ChassisConfig *cfg = chassis->cfg;
    ChassisTarget *t = &chassis->target;

    /* 机构停：有电机离线时目标改为 0，下面的斜坡让车受控减速 */
    const ChassisVel goal = chassis->measure.all_online ? *cmd : (ChassisVel){ 0 };

    /* 斜坡：限制加速度 */
    ramp_translation(&t->velocity, &goal, cfg->max_accel_m_s2 * dt_s);
    t->velocity.wz_rad_s =
        ramp_step(t->velocity.wz_rad_s, goal.wz_rad_s, cfg->max_alpha_rad_s2 * dt_s);

    /* 逆解：底盘速度 → 各轮目标 */
    switch (cfg->type)
    {
        case CHASSIS_OMNI:
            omni_inverse(&chassis->omni, &t->velocity, t->drive_speed_rad_s);
            break;
        case CHASSIS_MECANUM:
            mecanum_inverse(&cfg->mecanum, &t->velocity, t->drive_speed_rad_s);
            break;
        case CHASSIS_STEER:
        {
            SteerWheel wheel[CHASSIS_WHEELS];
            steer_inverse(&cfg->steer.kinematics, &t->velocity, chassis->measure.heading_rad,
                          wheel);
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                t->heading_rad[i] = wheel[i].heading_rad;
                t->drive_speed_rad_s[i] = wheel[i].speed_rad_s;
            }
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* 第 3 步：算输出                                                     */
/* ------------------------------------------------------------------ */

/* 把 x 限制在 [-limit, +limit] */
static float clamp(float x, float limit)
{
    return (x > limit) ? limit : ((x < -limit) ? -limit : x);
}

/* 离线的电机不写指令（电机组会填零力矩），并清积分，恢复时不猛冲 */
static void chassis_output_update(Chassis *chassis, float output_scale)
{
    const ChassisConfig *cfg = chassis->cfg;
    const ChassisMeasure *m = &chassis->measure;
    const ChassisTarget *t = &chassis->target;
    const float drive_limit_nm = cfg->drive_speed_pid.output_limit * output_scale;
    const float steer_limit_nm = cfg->steer.speed_pid.output_limit * output_scale;

    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        /* 驱动轮：速度环 */
        if (m->drive_online[i])
        {
            const float torque_nm =
                pid_step(&chassis->drive_pid[i], t->drive_speed_rad_s[i], m->drive_speed_rad_s[i]);
            motor_set_torque(chassis->drive[i], clamp(torque_nm, drive_limit_nm));
        }
        else
        {
            pid_reset(&chassis->drive_pid[i]);
        }

        /* 舵轮的转向电机：角度环 → 速度环 */
        if (cfg->type != CHASSIS_STEER)
        {
            continue;
        }
        if (m->steer_online[i])
        {
            const float rate_rad_s =
                pid_step(&chassis->steer_angle_pid[i], t->heading_rad[i], m->heading_rad[i]);
            const float torque_nm =
                pid_step(&chassis->steer_speed_pid[i], rate_rad_s, m->steer_speed_rad_s[i]);
            motor_set_torque(chassis->steer[i], clamp(torque_nm, steer_limit_nm));
        }
        else
        {
            pid_reset(&chassis->steer_angle_pid[i]);
            pid_reset(&chassis->steer_speed_pid[i]);
        }
    }
}
