/**
 * @file    omni.h
 * @brief   四轮全向轮底盘运动学：底盘速度 ↔ 各轮转速（纯计算，电脑上可测）
 * @note    几何约定（俯视，底盘系 C）：
 *          - 四个轮子到底盘中心的距离都是 center_dist_m，相邻两轮相差 90°；
 *          - 轮 0 在 first_wheel_rad 方向上（从 X 轴逆时针量），轮 1、2、3 依次逆时针排列。
 *            常见的 X 形布置：first_wheel_rad = π/4，轮 0–3 = 左前、左后、右后、右前；
 *            十字形布置：first_wheel_rad = 0，轮 0–3 = 前、左、后、右；
 *          - 轮子正转 = 这个轮子推动底盘绕中心逆时针转（沿切向）。电机输出轴正方向要用
 *            MotorConfig.direction 调成与此一致。
 *          轮 i 的转速：ω_i = (−sin θ_i · vx + cos θ_i · vy + R · wz) / r。
 */
#pragma once

#include "chassis_vel.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define OMNI_WHEELS 4u

typedef struct
{
    float wheel_radius_m;  /* 全向轮半径 */
    float center_dist_m;   /* 轮子接地点到底盘中心的距离 */
    float first_wheel_rad; /* 轮 0 所在方向，从 X 轴逆时针量 */
} OmniConfig;

typedef struct
{
    OmniConfig cfg;
    float sin_theta[OMNI_WHEELS]; /* 各轮方向角的正弦、余弦，初始化时算好 */
    float cos_theta[OMNI_WHEELS];
} Omni;

/** @pre wheel_radius_m > 0、center_dist_m > 0 */
void omni_init(Omni *omni, const OmniConfig *cfg);

/** 逆解：底盘速度 → 各轮转速（rad/s，正方向见文件说明） */
void omni_inverse(const Omni *omni, const ChassisVel *vel, float wheel_rad_s[OMNI_WHEELS]);

/**
 * @brief   正解：各轮转速 → 底盘速度
 * @note    四个轮子只有三个自由度，按最小二乘求解：轮子打滑、四个转速互相矛盾时得到“最接近”的底盘速度
 */
void omni_forward(const Omni *omni, const float wheel_rad_s[OMNI_WHEELS], ChassisVel *vel);

#ifdef __cplusplus
}
#endif
