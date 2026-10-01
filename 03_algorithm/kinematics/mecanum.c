/**
 * @file    mecanum.c
 * @brief   四轮麦克纳姆轮底盘运动学（公式见 mecanum.h）
 */
#include "mecanum.h"

/* 各轮系数：ω_i·r = vx + SIGN_VY[i]·vy + SIGN_WZ[i]·k·wz */
static const float SIGN_VY[MECANUM_WHEELS] = { -1.0f, 1.0f, -1.0f, 1.0f };
static const float SIGN_WZ[MECANUM_WHEELS] = { -1.0f, -1.0f, 1.0f, 1.0f };

void mecanum_inverse(const MecanumConfig *cfg, const ChassisVel *vel,
                     float wheel_rad_s[MECANUM_WHEELS])
{
    const float spin_m_s = (cfg->half_wheelbase_m + cfg->half_track_m) * vel->wz_rad_s;
    for (unsigned i = 0u; i < MECANUM_WHEELS; i++)
    {
        const float v_m_s = vel->vx_m_s + SIGN_VY[i] * vel->vy_m_s + SIGN_WZ[i] * spin_m_s;
        wheel_rad_s[i] = v_m_s / cfg->wheel_radius_m;
    }
}

/* 三列系数（全 1、SIGN_VY、SIGN_WZ）两两正交、模长平方都是 4，最小二乘解就是各列投影除以 4 */
void mecanum_forward(const MecanumConfig *cfg, const float wheel_rad_s[MECANUM_WHEELS],
                     ChassisVel *vel)
{
    float sum = 0.0f;
    float sum_vy = 0.0f;
    float sum_wz = 0.0f;
    for (unsigned i = 0u; i < MECANUM_WHEELS; i++)
    {
        const float v_m_s = wheel_rad_s[i] * cfg->wheel_radius_m;
        sum += v_m_s;
        sum_vy += SIGN_VY[i] * v_m_s;
        sum_wz += SIGN_WZ[i] * v_m_s;
    }
    const float n = (float)MECANUM_WHEELS;
    vel->vx_m_s = sum / n;
    vel->vy_m_s = sum_vy / n;
    vel->wz_rad_s = sum_wz / (n * (cfg->half_wheelbase_m + cfg->half_track_m));
}
