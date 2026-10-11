/**
 * @file    chassis.c
 * @brief   底盘子系统：读实测 → 算目标 → 算输出，三个函数按顺序排在下面（说明见 chassis.h）
 */
#include "chassis.h"

#include <math.h>

#include "03_algorithm/control/control.h"

/* 轮 i 有没有转向电机：舵轮全部有；半舵半全向只有舵轮那条对角线有 */
static bool has_steer_motor(const ChassisConfig *cfg, unsigned i)
{
    return cfg->type == CHASSIS_STEER
           || (cfg->type == CHASSIS_HALF_STEER && half_steer_is_steer(&cfg->half_steer, i));
}

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

        if (has_steer_motor(cfg, i))
        {
            if (!motor_supports_torque(steer[i]))
            {
                return false;
            }
            chassis->steer[i] = steer[i];
            pid_init(&chassis->steer_angle_pid[i], PID_POSITION, &cfg->steer.angle_pid);
            pid_init(&chassis->steer_speed_pid[i], PID_POSITION, &cfg->steer.speed_pid);
            chassis->zero[i].state =
                cfg->steer.zero_switch[i] != GPIO_IN_NONE ? STEER_ZERO_FORWARD : STEER_ZERO_DONE;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* 第 1 步：读实测                                                     */
/* ------------------------------------------------------------------ */

void chassis_measure_update(Chassis *chassis)
{
    ChassisMeasure *m = &chassis->measure;
    const ChassisConfig *cfg = chassis->cfg;

    m->all_online = true;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        MotorFeedback fb;
        m->drive_online[i] = motor_read_feedback(chassis->drive[i], &fb);
        m->drive_speed_rad_s[i] = fb.speed_rad_s;
        m->wheel_ok[i] = m->drive_online[i];

        if (has_steer_motor(cfg, i))
        {
            SteerZero *z = &chassis->zero[i];
            const GpioInput sw = cfg->steer.zero_switch[i];
            m->steer_online[i] = motor_read_feedback(chassis->steer[i], &fb);
            m->steer_angle_rad[i] = fb.angle_rad;
            m->heading_rad[i] = fb.angle_rad - z->center_rad - cfg->steer.zero_rad[i];
            m->steer_speed_rad_s[i] = fb.speed_rad_s;
            m->zero_switch_active[i] =
                sw != GPIO_IN_NONE && gpio_read(sw) != cfg->steer.zero_switch_active_low;
            /* 接了开关的转向电机离线：重连后多圈角度从 0 重新算，零点作废，上线后重找 */
            if (sw != GPIO_IN_NONE && !m->steer_online[i])
            {
                *z = (SteerZero){ .state = STEER_ZERO_FORWARD };
            }
            m->wheel_ok[i] = m->wheel_ok[i] && m->steer_online[i] && z->state == STEER_ZERO_DONE;
        }
        m->all_online = m->all_online && m->wheel_ok[i];
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
        case CHASSIS_HALF_STEER:
        {
            SteerWheel wheel[CHASSIS_WHEELS];
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                wheel[i] = (SteerWheel){ .heading_rad = m->heading_rad[i],
                                         .speed_rad_s = m->drive_speed_rad_s[i] };
            }
            half_steer_forward(&cfg->half_steer, wheel, &m->velocity);
            break;
        }
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
void chassis_target_update(Chassis *chassis, const ChassisVel *cmd, bool stop_all)
{
    const ChassisConfig *cfg = chassis->cfg;
    ChassisTarget *t = &chassis->target;

    /* 全车停：目标从当前实测速度开始，解锁时车还在滑行，目标也不会突然跳到 0 或摇杆值 */
    t->stop_all = stop_all;
    if (stop_all)
    {
        t->velocity = chassis->measure.velocity;
        return;
    }

    /* 降级：有轮子不能用时其余轮子继续，目标速度和加速度都打折（ADR 0073） */
    const float scale = chassis->measure.all_online ? 1.0f : CHASSIS_DEGRADED_SCALE;
    const ChassisVel goal = { .vx_m_s = cmd->vx_m_s * scale,
                              .vy_m_s = cmd->vy_m_s * scale,
                              .wz_rad_s = cmd->wz_rad_s * scale };

    /* 斜坡：限制加速度 */
    ramp_translation(&t->velocity, &goal, cfg->max_accel_m_s2 * scale * CHASSIS_PERIOD_S);
    t->velocity.wz_rad_s = ramp_step(t->velocity.wz_rad_s, goal.wz_rad_s,
                                     cfg->max_alpha_rad_s2 * scale * CHASSIS_PERIOD_S);

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
        case CHASSIS_HALF_STEER:
        {
            SteerWheel wheel[CHASSIS_WHEELS];
            half_steer_inverse(&cfg->half_steer, &t->velocity, chassis->measure.heading_rad, wheel);
            for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
            {
                t->heading_rad[i] = wheel[i].heading_rad; /* 全向轮的朝向不用 */
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

/*
 * 找零一拍（ADR 0068）：按开关电平的变化记边沿，返回转向电机的目标转速。
 * 一开始就在开关里时，先看到的“离开”不算，等下一次完整的进入、离开
 */
static float steer_zero_step(SteerZero *z, const ChassisSteerConfig *sc, float angle_rad,
                             bool active)
{
    if (!z->started)
    {
        z->started = true;
        z->was_active = active; /* 在开关里起步时，不把第一拍当成“进入” */
    }
    z->elapsed_s += CHASSIS_PERIOD_S;
    if (z->elapsed_s > sc->zero_search_timeout_s)
    {
        z->state = STEER_ZERO_FAILED;
        return 0.0f;
    }
    if (active && !z->was_active)
    {
        z->enter_rad = angle_rad;
        z->seen_enter = true;
    }
    else if (!active && z->was_active && z->seen_enter)
    {
        const float mid_rad = 0.5f * (z->enter_rad + angle_rad);
        z->seen_enter = false;
        if (z->state == STEER_ZERO_FORWARD)
        {
            z->forward_mid_rad = mid_rad;
            z->state = STEER_ZERO_REVERSE; /* 反方向再经过一次，抵消轮询滞后 */
        }
        else
        {
            z->center_rad = 0.5f * (z->forward_mid_rad + mid_rad);
            z->state = STEER_ZERO_DONE;
        }
    }
    z->was_active = active;
    if (z->state == STEER_ZERO_FORWARD)
    {
        return sc->zero_search_rad_s;
    }
    return z->state == STEER_ZERO_REVERSE ? -sc->zero_search_rad_s : 0.0f;
}

/* 不能用的轮子、离线的转向电机不写指令（电机组会填零力矩），并清积分，恢复时不猛冲 */
void chassis_output_update(Chassis *chassis, float output_scale)
{
    const ChassisConfig *cfg = chassis->cfg;
    const ChassisMeasure *m = &chassis->measure;
    const ChassisTarget *t = &chassis->target;

    /* 全车停：不写指令（发送出口统一改写成停机动作），清积分 */
    if (t->stop_all)
    {
        for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
        {
            pid_reset(&chassis->drive_pid[i]);
            pid_reset(&chassis->steer_angle_pid[i]);
            pid_reset(&chassis->steer_speed_pid[i]);
        }
        return;
    }
    const float drive_limit_nm = cfg->drive_speed_pid.output_limit * output_scale;
    const float steer_limit_nm = cfg->steer.speed_pid.output_limit * output_scale;

    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        /* 驱动轮：速度环（转向电机离线的舵轮也不驱动：朝向不受控） */
        if (m->wheel_ok[i])
        {
            const float torque_nm =
                pid_step(&chassis->drive_pid[i], t->drive_speed_rad_s[i], m->drive_speed_rad_s[i]);
            motor_set_torque(chassis->drive[i], clamp(torque_nm, drive_limit_nm));
        }
        else
        {
            pid_reset(&chassis->drive_pid[i]);
        }

        /* 转向电机（舵轮）：角度环 → 速度环 */
        if (!has_steer_motor(cfg, i))
        {
            continue;
        }
        SteerZero *z = &chassis->zero[i];
        if (m->steer_online[i]
            && (z->state == STEER_ZERO_FORWARD || z->state == STEER_ZERO_REVERSE))
        {
            /* 找零：转向电机按固定转速转过开关（速度环），驱动轮不出力（wheel_ok 为 false） */
            const float rate_rad_s =
                steer_zero_step(z, &cfg->steer, m->steer_angle_rad[i], m->zero_switch_active[i]);
            const float torque_nm =
                pid_step(&chassis->steer_speed_pid[i], rate_rad_s, m->steer_speed_rad_s[i]);
            motor_set_torque(chassis->steer[i], clamp(torque_nm, steer_limit_nm));
        }
        else if (m->steer_online[i] && z->state == STEER_ZERO_DONE)
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
