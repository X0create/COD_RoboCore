/**
 * @file    kinematics.c
 * @brief   底盘运动学：底盘速度 ↔ 各轮转速 / 朝向（说明见 kinematics.h）
 */
#include "kinematics.h"

#include <math.h>

#include "03_algorithm/math/rm_math.h"

/* ================================================================== */
/* 全向轮 omni（说明见 kinematics.h 同名一节） */
/* ================================================================== */

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

/* ================================================================== */
/* 麦轮 mecanum（说明见 kinematics.h 同名一节） */
/* ================================================================== */

/* 各轮系数：ω_i·r = vx + SIGN_VY[i]·vy + SIGN_WZ[i]·k·wz */
static const float SIGN_VY[MECANUM_WHEELS] = { -1.0f, 1.0f, -1.0f, 1.0f };
static const float SIGN_WZ[MECANUM_WHEELS] = { -1.0f, -1.0f, 1.0f, 1.0f };

/* 逆解：底盘速度 → 每个轮子的转速（自转项的力臂是半轴距 + 半轮距） */
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

/* ================================================================== */
/* 舵轮 steer（说明见 kinematics.h 同名一节） */
/* ================================================================== */

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

/* ================================================================== */
/* 半舵半全向 half_steer（说明见 kinematics.h 同名一节） */
/* ================================================================== */

/* 轮子接地点位置的符号 SIGN_X / SIGN_Y 与舵轮共用（上一节，轮序相同：左前、左后、右后、右前） */
_Static_assert(HALF_STEER_WHEELS == STEER_WHEELS, "半舵半全向与舵轮共用轮位符号表");

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
