/**
 * @file    half_steer.h
 * @brief   半舵半全向底盘运动学：一条对角线上两个舵轮、另一条对角线上两个全向轮（纯计算，电脑上可测）
 * @note    几何约定（俯视，底盘系 C，与 steer.h 相同）：
 *          - 轮 0–3 = 左前、左后、右后、右前（逆时针），四个轮子的接地点在 (±half_wheelbase_m, ±half_track_m)；
 *          - 舵轮：朝向 heading 是轮子向前滚动的方向与 X 轴的夹角，逆时针为正；正转 = 沿 heading 滚动；
 *            逆解规则同 steer.h（最多转 90°，超过就反转轮子）；
 *          - 全向轮：沿切向安装（滚动方向垂直于它到中心的连线，正方形底盘就是 45° 斜装），
 *            正转 = 推动底盘绕中心逆时针转，与 omni.h 相同。
 *          轮 i 接地点的速度：v_i = (vx − wz·y_i, vy + wz·x_i)。
 *          全向轮 i 的转速：ω_i = (−y_i·vx + x_i·vy + R_i²·wz) / (R_i·r_omni)，R_i 是它到中心的距离。
 */
#pragma once

#include <stdbool.h>

#include "chassis_vel.h"
#include "steer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define HALF_STEER_WHEELS 4u

/** 舵轮在哪条对角线上（另一条是全向轮） */
typedef enum
{
    HALF_STEER_LF_RB, /* 舵轮：左前（轮 0）、右后（轮 2）；全向轮：左后、右前 */
    HALF_STEER_LB_RF, /* 舵轮：左后（轮 1）、右前（轮 3）；全向轮：左前、右后 */
} HalfSteerDiagonal;

typedef struct
{
    HalfSteerDiagonal steer_diagonal;
    float steer_radius_m;   /* 舵轮半径 */
    float omni_radius_m;    /* 全向轮半径 */
    float half_wheelbase_m; /* 前后轮接地点距离的一半 */
    float half_track_m;     /* 左右轮接地点距离的一半 */
} HalfSteerConfig;

/** 轮 wheel（0–3）是不是舵轮 */
bool half_steer_is_steer(const HalfSteerConfig *cfg, unsigned wheel);

/**
 * @brief   逆解：底盘速度 → 各轮目标
 * @param   heading_rad  各轮当前朝向；只用舵轮的，全向轮的值不读
 * @param   out          舵轮：目标朝向和转速；全向轮：只有转速有意义（朝向原样填当前值）
 * @pre     各尺寸 > 0
 */
void half_steer_inverse(const HalfSteerConfig *cfg, const ChassisVel *vel,
                        const float heading_rad[HALF_STEER_WHEELS],
                        SteerWheel out[HALF_STEER_WHEELS]);

/**
 * @brief   正解：各轮实测 → 底盘速度
 * @param   wheel  舵轮用朝向和转速，全向轮只用转速
 * @note    舵轮每个给两个方程、全向轮每个给一个，共 6 个方程 3 个未知数，取最小二乘解（打滑时得到“最接近”的速度）
 */
void half_steer_forward(const HalfSteerConfig *cfg, const SteerWheel wheel[HALF_STEER_WHEELS],
                        ChassisVel *vel);

#ifdef __cplusplus
}
#endif
