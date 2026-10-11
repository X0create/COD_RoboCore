/**
 * @file    power.c
 * @brief   底盘功率控制的算法（说明见 power.h）
 */
#include "power.h"

#include <math.h>

/* ================================================================== */
/* 递推最小二乘 RLS（说明见 power.h 同名一节）                          */
/* ================================================================== */

/* 参数初值 w0，协方差 P 初值为 p0·I（p0 越大，开始时参数变化越快） */
void rls_init(Rls *rls, uint8_t n, float lambda, float p0, const float w0[])
{
    rls->n = n;
    rls->lambda = lambda;
    for (uint32_t r = 0u; r < n; r++)
    {
        rls->w[r] = w0[r];
        for (uint32_t c = 0u; c < n; c++)
        {
            rls->p[r * n + c] = (r == c) ? p0 : 0.0f;
        }
    }
}

/* 用当前参数预测：y = wᵀ x */
float rls_predict(const Rls *rls, const float x[])
{
    float y = 0.0f;
    for (uint32_t i = 0u; i < rls->n; i++)
    {
        y += rls->w[i] * x[i];
    }
    return y;
}

float rls_update(Rls *rls, const float x[], float y)
{
    const uint32_t n = rls->n;
    /* 先算 P x 和 denom = λ + xᵀ P x，增益 K = P x / denom */
    float px[RLS_MAX_N];
    float denom = rls->lambda;

    for (uint32_t r = 0u; r < n; r++)
    {
        px[r] = 0.0f;
        for (uint32_t c = 0u; c < n; c++)
        {
            px[r] += rls->p[r * n + c] * x[c];
        }
        denom += x[r] * px[r];
    }

    const float e = y - rls_predict(rls, x); /* 预测误差 */
    for (uint32_t r = 0u; r < n; r++)
    {
        rls->w[r] += px[r] / denom * e; /* K = P x / denom */
    }
    /* P 对称，所以 xᵀP = (P x)ᵀ，P − K (P x)ᵀ = P − (P x)(P x)ᵀ / denom */
    for (uint32_t r = 0u; r < n; r++)
    {
        for (uint32_t c = 0u; c < n; c++)
        {
            /* 除以遗忘因子 λ（< 1）：旧数据的权重逐渐变小，参数能跟上缓慢的变化 */
            rls->p[r * n + c] = (rls->p[r * n + c] - px[r] * px[c] / denom) / rls->lambda;
        }
    }
    return e;
}

/* ================================================================== */
/* 功率模型（说明见 power.h 同名一节）                                  */
/* ================================================================== */

/* 辨识出的 k1、k2 至少取这么大：k2 是功率分配二次方程的二次项系数，不能为 0 */
#define POWER_K_MIN 1e-6f

float power_model_motor(const PowerModel *model, float torque_nm, float speed_rad_s, unsigned n)
{
    return torque_nm * speed_rad_s + model->k1 * fabsf(speed_rad_s)
           + model->k2 * torque_nm * torque_nm + model->k3 / (float)n;
}

void power_model_identify(Rls *rls, PowerModel *model, const float torque_nm[],
                          const float speed_rad_s[], unsigned n, float measured_w)
{
    float sum_abs_speed = 0.0f;
    float sum_torque2 = 0.0f;
    float mech_w = 0.0f;
    for (unsigned i = 0u; i < n; i++)
    {
        sum_abs_speed += fabsf(speed_rad_s[i]);
        sum_torque2 += torque_nm[i] * torque_nm[i];
        mech_w += torque_nm[i] * speed_rad_s[i];
    }
    const float x[2] = { sum_abs_speed, sum_torque2 };
    (void)rls_update(rls, x, measured_w - mech_w - model->k3);
    model->k1 = fmaxf(rls->w[0], POWER_K_MIN);
    model->k2 = fmaxf(rls->w[1], POWER_K_MIN);
}

/* ================================================================== */
/* 功率分配（说明见 power.h 同名一节）                                  */
/* ================================================================== */

#define POWER_MAX_MOTORS  8u
#define POWER_STEER_SHARE 0.8f /* 舵轮：转向电机最多用总功率的这一份 */

/*
 * 给第 i 台电机分到 power_w 时的力矩：解 k2τ² + ωτ + (k1|ω| + k3/n − power_w) = 0，取与原指令同号的根；
 * 无实根时取功率最小点 −ω/(2k2)。结果限制在 0 到原指令之间：只压缩不放大，也不反向
 * （分到的功率比空转损耗还小时，解会是反向制动的力矩，这里改为 0，避免指令加速、输出刹车来回抖）
 */
static float solve_torque(const PowerModel *model, const PowerMotor *m, unsigned n, float power_w)
{
    const float w = m->speed_rad_s;
    const float c = model->k1 * fabsf(w) + model->k3 / (float)n - power_w;
    const float delta = w * w - 4.0f * model->k2 * c;
    float torque_nm;
    if (delta <= 0.0f)
    {
        torque_nm = -w / (2.0f * model->k2);
    }
    else
    {
        const float root = sqrtf(delta);
        torque_nm = (m->torque_nm >= 0.0f) ? (-w + root) / (2.0f * model->k2)
                                           : (-w - root) / (2.0f * model->k2);
    }
    if (torque_nm * m->torque_nm <= 0.0f)
    {
        return 0.0f;
    }
    return (fabsf(torque_nm) < fabsf(m->torque_nm)) ? torque_nm : m->torque_nm;
}

float power_limit(const PowerModel *model, const PowerAllocConfig *cfg, const PowerMotor motors[],
                  unsigned n, float max_power_w, float out_torque_nm[])
{
    float cmd_w[POWER_MAX_MOTORS];
    float sum_cmd_w = 0.0f; /* 全部在线电机的模型功率之和（含回馈的负值） */
    float sum_required_w = 0.0f; /* 只算要耗电（> 0）的那些 */
    float sum_err = 0.0f;        /* 同上，转速误差之和 */
    float allocatable_w = max_power_w;

    for (unsigned i = 0u; i < n; i++)
    {
        cmd_w[i] = 0.0f;
        if (!motors[i].online)
        {
            continue;
        }
        cmd_w[i] = power_model_motor(model, motors[i].torque_nm, motors[i].speed_rad_s, n);
        sum_cmd_w += cmd_w[i];
        if (cmd_w[i] <= 0.0f)
        {
            allocatable_w -= cmd_w[i]; /* 回馈的功率可以分给别的电机 */
        }
        else
        {
            sum_required_w += cmd_w[i];
            sum_err += fabsf(motors[i].speed_err_rad_s);
        }
    }

    /* 没超上限：原样输出（离线的为 0） */
    if (sum_cmd_w <= max_power_w)
    {
        for (unsigned i = 0u; i < n; i++)
        {
            out_torque_nm[i] = motors[i].online ? motors[i].torque_nm : 0.0f;
        }
        return sum_cmd_w;
    }

    /* 误差权重的置信度：Σ误差 在 [err_low, err_high] 内从 0 线性升到 1 */
    float confidence = (sum_err - cfg->err_low_rad_s) / (cfg->err_high_rad_s - cfg->err_low_rad_s);
    confidence = fminf(fmaxf(confidence, 0.0f), 1.0f);

    for (unsigned i = 0u; i < n; i++)
    {
        if (!motors[i].online)
        {
            out_torque_nm[i] = 0.0f;
            continue;
        }
        if (cmd_w[i] <= 0.0f)
        {
            out_torque_nm[i] = motors[i].torque_nm; /* 在回馈：不压缩 */
            continue;
        }
        const float weight_err =
            (sum_err > 0.0f) ? fabsf(motors[i].speed_err_rad_s) / sum_err : 0.0f;
        const float weight_prop = cmd_w[i] / sum_required_w;
        const float weight = confidence * weight_err + (1.0f - confidence) * weight_prop;
        out_torque_nm[i] = solve_torque(model, &motors[i], n, weight * allocatable_w);
    }
    return sum_cmd_w;
}

float power_steer_budget(float steer_cmd_power_w, float max_power_w)
{
    return fminf(steer_cmd_power_w, max_power_w * POWER_STEER_SHARE);
}
