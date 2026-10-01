/**
 * @file    steer.c
 * @brief   四轮舵轮底盘运动学（约定见 steer.h）
 */
#include "steer.h"
#include "03_algorithm/math/math_const.h"

#include <math.h>

/* 转向轴位置的符号：左前、左后、右后、右前 */
static const float SIGN_X[STEER_WHEELS] = { 1.0f, -1.0f, -1.0f, 1.0f };
static const float SIGN_Y[STEER_WHEELS] = { 1.0f, 1.0f, -1.0f, -1.0f };

/* 把角度差换到 (−π, π] */
static float wrap_pi(float a)
{
    a = fmodf(a + RM_PI, 2.0f * RM_PI);
    if (a <= 0.0f)
    {
        a += 2.0f * RM_PI;
    }
    return a - RM_PI;
}

/*
 * 单个舵轮：轮子所在点的速度 = 底盘平移速度 + 自转速度 × 该点到中心的距离（ω × r，方向垂直于 r）。
 * 轮子朝向对准这个速度方向，转速 = 速度大小 / 轮半径
 */
SteerWheel steer_wheel_inverse(float x_m, float y_m, float wheel_radius_m, const ChassisVel *vel,
                               float heading_rad)
{
    const float vx = vel->vx_m_s - vel->wz_rad_s * y_m;
    const float vy = vel->vy_m_s + vel->wz_rad_s * x_m;
    const float speed_m_s = sqrtf(vx * vx + vy * vy);
    if (speed_m_s < STEER_MIN_SPEED_M_S) /* 几乎不动：保持当前朝向，不让舵向乱转 */
    {
        return (SteerWheel){ .heading_rad = heading_rad, .speed_rad_s = 0.0f };
    }

    /* 要转的角度超过 90° 时，改成反方向少转一点、轮子倒转：舵向最多转 90° */
    float delta = wrap_pi(atan2f(vy, vx) - heading_rad);
    float speed_rad_s = speed_m_s / wheel_radius_m;
    if (delta > RM_HALF_PI)
    {
        delta -= RM_PI;
        speed_rad_s = -speed_rad_s;
    }
    else if (delta < -RM_HALF_PI)
    {
        delta += RM_PI;
        speed_rad_s = -speed_rad_s;
    }
    return (SteerWheel){ .heading_rad = heading_rad + delta, .speed_rad_s = speed_rad_s };
}

/* 逆解：四个舵轮各自按 steer_wheel_inverse() 计算，转向轴在 (±half_wheelbase, ±half_track) */
void steer_inverse(const SteerConfig *cfg, const ChassisVel *vel,
                   const float heading_rad[STEER_WHEELS], SteerWheel out[STEER_WHEELS])
{
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        out[i] =
            steer_wheel_inverse(SIGN_X[i] * cfg->half_wheelbase_m, SIGN_Y[i] * cfg->half_track_m,
                                cfg->wheel_radius_m, vel, heading_rad[i]);
    }
}

/*
 * 未知量 (vx, vy, wz)，每个轮子两个方程。转向轴关于中心对称（Σx = Σy = 0）时法方程是对角的：
 * vx、vy 是各轮速度分量的平均，wz = Σ(x·v_y − y·v_x) / Σ(x² + y²)
 */
void steer_forward(const SteerConfig *cfg, const SteerWheel wheel[STEER_WHEELS], ChassisVel *vel)
{
    float sum_vx = 0.0f;
    float sum_vy = 0.0f;
    float sum_moment = 0.0f;
    for (unsigned i = 0u; i < STEER_WHEELS; i++)
    {
        const float v_m_s = wheel[i].speed_rad_s * cfg->wheel_radius_m;
        const float vx = v_m_s * cosf(wheel[i].heading_rad);
        const float vy = v_m_s * sinf(wheel[i].heading_rad);
        sum_vx += vx;
        sum_vy += vy;
        sum_moment += SIGN_X[i] * cfg->half_wheelbase_m * vy - SIGN_Y[i] * cfg->half_track_m * vx;
    }
    const float n = (float)STEER_WHEELS;
    const float a = cfg->half_wheelbase_m;
    const float b = cfg->half_track_m;
    vel->vx_m_s = sum_vx / n;
    vel->vy_m_s = sum_vy / n;
    vel->wz_rad_s = sum_moment / (n * (a * a + b * b));
}
