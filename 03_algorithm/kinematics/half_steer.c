/**
 * @file    half_steer.c
 * @brief   半舵半全向底盘运动学（约定见 half_steer.h）
 */
#include "half_steer.h"

#include <math.h>

/* 轮子接地点位置的符号：左前、左后、右后、右前（同 steer.c） */
static const float SIGN_X[HALF_STEER_WHEELS] = { 1.0f, -1.0f, -1.0f, 1.0f };
static const float SIGN_Y[HALF_STEER_WHEELS] = { 1.0f, 1.0f, -1.0f, -1.0f };

bool half_steer_is_steer(const HalfSteerConfig *cfg, unsigned wheel)
{
    /* 左前、右后是偶数号轮（0、2），左后、右前是奇数号轮（1、3） */
    const bool even = (wheel % 2u) == 0u;
    return (cfg->steer_diagonal == HALF_STEER_LF_RB) ? even : !even;
}

void half_steer_inverse(const HalfSteerConfig *cfg, const ChassisVel *vel,
                        const float heading_rad[HALF_STEER_WHEELS],
                        SteerWheel out[HALF_STEER_WHEELS])
{
    for (unsigned i = 0u; i < HALF_STEER_WHEELS; i++)
    {
        const float x = SIGN_X[i] * cfg->half_wheelbase_m;
        const float y = SIGN_Y[i] * cfg->half_track_m;
        if (half_steer_is_steer(cfg, i))
        {
            out[i] = steer_wheel_inverse(x, y, cfg->steer_radius_m, vel, heading_rad[i]);
            continue;
        }
        /* 全向轮只能沿切向出力：接地点速度在切向 (−y, x)/R 上的分量，除以轮半径 */
        const float r = sqrtf(x * x + y * y);
        const float v_m_s = (-y * vel->vx_m_s + x * vel->vy_m_s) / r + r * vel->wz_rad_s;
        out[i] = (SteerWheel){ .heading_rad = heading_rad[i],
                               .speed_rad_s = v_m_s / cfg->omni_radius_m };
    }
}

/* 3×3 行列式（按第一行展开） */
static float det3(const float m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
           - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
           + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* 把一个方程 a·(vx, vy, wz) = b 累加进法方程 AᵀA·u = Aᵀb */
static void add_equation(float ata[3][3], float atb[3], const float a[3], float b)
{
    for (int r = 0; r < 3; r++)
    {
        for (int c = 0; c < 3; c++)
        {
            ata[r][c] += a[r] * a[c];
        }
        atb[r] += a[r] * b;
    }
}

/*
 * 方程（u = (vx, vy, wz)）：
 *   舵轮 i：  vx − y_i·wz = v_i·cos θ_i，  vy + x_i·wz = v_i·sin θ_i
 *   全向轮 i：(−y_i·vx + x_i·vy)/R_i + R_i·wz = ω_i·r_omni
 * 累加成法方程后用克拉默法则解（两个舵轮就已经让 AᵀA 可逆，所以行列式不为 0）
 */
void half_steer_forward(const HalfSteerConfig *cfg, const SteerWheel wheel[HALF_STEER_WHEELS],
                        ChassisVel *vel)
{
    float ata[3][3] = { { 0.0f } };
    float atb[3] = { 0.0f };
    for (unsigned i = 0u; i < HALF_STEER_WHEELS; i++)
    {
        const float x = SIGN_X[i] * cfg->half_wheelbase_m;
        const float y = SIGN_Y[i] * cfg->half_track_m;
        if (half_steer_is_steer(cfg, i))
        {
            const float v_m_s = wheel[i].speed_rad_s * cfg->steer_radius_m;
            add_equation(ata, atb, (const float[3]){ 1.0f, 0.0f, -y },
                         v_m_s * cosf(wheel[i].heading_rad));
            add_equation(ata, atb, (const float[3]){ 0.0f, 1.0f, x },
                         v_m_s * sinf(wheel[i].heading_rad));
        }
        else
        {
            const float r = sqrtf(x * x + y * y);
            add_equation(ata, atb, (const float[3]){ -y / r, x / r, r },
                         wheel[i].speed_rad_s * cfg->omni_radius_m);
        }
    }

    /* 克拉默法则：第 k 个未知数 = 把 AᵀA 的第 k 列换成 Aᵀb 后的行列式 / AᵀA 的行列式 */
    const float d = det3(ata);
    float u[3];
    for (int k = 0; k < 3; k++)
    {
        float m[3][3];
        for (int r = 0; r < 3; r++)
        {
            for (int c = 0; c < 3; c++)
            {
                m[r][c] = (c == k) ? atb[r] : ata[r][c];
            }
        }
        u[k] = det3(m) / d;
    }
    vel->vx_m_s = u[0];
    vel->vy_m_s = u[1];
    vel->wz_rad_s = u[2];
}
