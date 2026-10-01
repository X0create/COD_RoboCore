/**
 * @file    omni.c
 * @brief   四轮全向轮底盘运动学（公式见 omni.h）
 */
#include "omni.h"
#include "03_algorithm/math/math_const.h"

#include <math.h>

/* 预先算好每个轮子安装角的 sin、cos：第 i 个轮子在 first_wheel_rad + i·90° 的位置 */
void omni_init(Omni *omni, const OmniConfig *cfg)
{
    omni->cfg = *cfg;
    for (unsigned i = 0u; i < OMNI_WHEELS; i++)
    {
        const float theta = cfg->first_wheel_rad + (float)i * RM_HALF_PI;
        omni->sin_theta[i] = sinf(theta);
        omni->cos_theta[i] = cosf(theta);
    }
}

/*
 * 逆解：底盘速度 → 每个轮子的转速。轮子只能沿自己的切线方向出力：
 * 轮缘线速度 = 底盘平移速度在切线方向的分量 + 自转带来的线速度（中心距 × wz），再除以轮半径
 */
void omni_inverse(const Omni *omni, const ChassisVel *vel, float wheel_rad_s[OMNI_WHEELS])
{
    const float spin_m_s = omni->cfg.center_dist_m * vel->wz_rad_s;
    for (unsigned i = 0u; i < OMNI_WHEELS; i++)
    {
        const float v_m_s =
            -omni->sin_theta[i] * vel->vx_m_s + omni->cos_theta[i] * vel->vy_m_s + spin_m_s;
        wheel_rad_s[i] = v_m_s / omni->cfg.wheel_radius_m;
    }
}

/*
 * 相邻两轮相差 90° 时，Σ sin²θ = Σ cos²θ = 2，Σ sinθ = Σ cosθ = Σ sinθ·cosθ = 0，
 * 最小二乘的法方程是对角的，直接得到：vx = −½ Σ sinθ·v，vy = ½ Σ cosθ·v，wz = Σ v / (4R)
 */
void omni_forward(const Omni *omni, const float wheel_rad_s[OMNI_WHEELS], ChassisVel *vel)
{
    float sum_sin = 0.0f;
    float sum_cos = 0.0f;
    float sum = 0.0f;
    for (unsigned i = 0u; i < OMNI_WHEELS; i++)
    {
        const float v_m_s = wheel_rad_s[i] * omni->cfg.wheel_radius_m;
        sum_sin += omni->sin_theta[i] * v_m_s;
        sum_cos += omni->cos_theta[i] * v_m_s;
        sum += v_m_s;
    }
    vel->vx_m_s = -0.5f * sum_sin;
    vel->vy_m_s = 0.5f * sum_cos;
    vel->wz_rad_s = sum / ((float)OMNI_WHEELS * omni->cfg.center_dist_m);
}
