/**
 * @file    power.h
 * @brief   底盘功率控制的算法（纯计算，电脑上可测）：电机功率模型与参数辨识、功率分配
 * @note    为什么要功率控制：裁判系统规定底盘功率上限，超了会扣血；所以在电机出力之前先估算会用多少功率，
 *          超了就按比例把各电机的力矩压下来。
 *          按节排列：功率模型、功率分配；参数辨识用同目录的通用 RLS（rls.h）。
 *          底盘在第 3 步“速度环算出力矩之后、写给电机之前”调用（目前还没接进底盘）。
 *          方法参考港科大 ENTERPRIZE 战队开源的 RM2024 超级电容 / 功率控制方案（RM2024-PowerModule）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "03_algorithm/power/rls.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================== */
/* 功率模型                                                            */
/*
 * 一台电机从电池取的功率（港科大模型）：Pᵢ = τᵢωᵢ + k1·|ωᵢ| + k2·τᵢ² + k3/n
 *   τ、ω 是输出轴力矩（N·m）、转速（rad/s）；τω 是机械功率，k1|ω| 近似摩擦与铁损，k2τ² 近似铜损，k3 是整组的静态损耗。
 * 另一种常见写法是 k1·τ² + k2·ω²（转速项为 ω²）；这里先用上面的形式，哪个更准要在台架上辨识对比。
 */
/* ================================================================== */

typedef struct
{
    float k1; /* W / (rad/s)，≥ 0 */
    float k2; /* W / (N·m)²，> 0（功率分配要解以它为二次项系数的方程） */
    float k3; /* W，整组电机的静态损耗 */
} PowerModel;

/** 一台电机的模型功率；n 是这一组的电机数（k3 平均分给每台） */
float power_model_motor(const PowerModel *model, float torque_nm, float speed_rad_s, unsigned n);

/**
 * @brief   用实测总功率在线辨识 k1、k2（k3 不辨识，失能全部电机时实测）
 * @param   rls          2 个参数的 RLS（rls_init 时 w0 = {k1, k2} 的初值）
 * @param   torque_nm    各电机实测力矩（反馈，不是指令）
 * @param   speed_rad_s  各电机实测转速
 * @param   measured_w   同一时刻的实测总功率（超级电容或裁判系统），只在有效时调用
 * @note    观测方程：measured − Σ τω − k3 = k1·Σ|ω| + k2·Στ²。k1、k2 不会被更新成负数或 0
 */
void power_model_identify(Rls *rls, PowerModel *model, const float torque_nm[],
                          const float speed_rad_s[], unsigned n, float measured_w);

/* ================================================================== */
/* 功率分配                                                            */
/*
 * 一组电机的模型总功率超过上限时，按权重把上限分给各电机，再解 k2τ² + ωτ + (k1|ω| + k3/n − Pᵢ) = 0 反算力矩：
 *   - 权重：转速误差大时按误差比例分（先让跟不上的轮子用功率），误差小时按原指令功率比例分，
 *     两者之间按 Σ误差 在 [err_low, err_high] 里的位置线性过渡（避免在坡上抖动）；
 *   - 模型功率为负（电机在回馈）的不压缩，它回馈的功率加进可分配的总量；
 *   - 根取与原指令同号的那个；无实根时取功率最小的 τ = −ω / (2k2)；
 *   - 结果限制在 0 到原指令之间：只压缩、不放大，也不反向（直接解方程可能解出比原指令更大、或方向相反的力矩）。
 * 舵轮两组电机：转向电机先用 power_steer_budget() 分到最多 80%，驱动电机用剩下的（转向不到位时驱动也没用）。
 */
/* ================================================================== */

typedef struct
{
    float err_low_rad_s;  /* Σ|转速误差| 低于它：完全按指令功率比例分配 */
    float err_high_rad_s; /* 高于它：完全按误差比例分配 */
} PowerAllocConfig;

typedef struct
{
    bool online;           /* 离线的电机：输出 0，不参与分配 */
    float torque_nm;       /* 速度环算出的力矩指令 */
    float speed_rad_s;     /* 实测转速 */
    float speed_err_rad_s; /* 目标转速 − 实测转速（取绝对值用） */
} PowerMotor;

/**
 * @brief   把一组电机的力矩压到 max_power_w 以内
 * @param   out_torque_nm  n 个：压缩后的力矩（没超上限时等于原指令）；可以和输入不是同一块内存
 * @return  压缩前的模型总功率（W），供调试和舵轮分预算用
 * @pre     model->k2 > 0；n ≤ 8；cfg->err_high_rad_s > cfg->err_low_rad_s
 */
float power_limit(const PowerModel *model, const PowerAllocConfig *cfg, const PowerMotor motors[],
                  unsigned n, float max_power_w, float out_torque_nm[]);

/** 舵轮：转向电机这一组本周期能用的功率 = min(指令功率, 上限 × 0.8)；驱动电机用 max_power_w 减去它 */
float power_steer_budget(float steer_cmd_power_w, float max_power_w);

#ifdef __cplusplus
}
#endif
