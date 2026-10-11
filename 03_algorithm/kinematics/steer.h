/**
 * @file    steer.h
 * @brief   四轮舵轮底盘运动学：底盘速度 ↔ 各轮朝向与转速（纯计算，电脑上可测）
 * @note    几何约定（俯视，底盘系 C）：
 *          - 轮 0–3 = 左前、左后、右后、右前（逆时针，与 mecanum.h 同序），转向轴在
 *            (±half_wheelbase_m, ±half_track_m)；
 *          - 轮子朝向 heading：轮子向前滚动的方向与底盘 X 轴的夹角，俯视逆时针为正；
 *          - 轮子正转 = 沿 heading 方向滚动。
 *          轮 i 转向轴处的速度：v_i = (vx − wz·y_i, vy + wz·x_i)。
 *          逆解给出的目标朝向离当前朝向最多 90°：需要转过 90° 以上时，改为反向转动轮子、转向另一侧，
 *          转向电机转得少、换向快；目标朝向是连续值（当前朝向 + 差值），不会在 ±π 处跳变。
 */
#pragma once

#include "chassis_vel.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define STEER_WHEELS 4u

/** 转向轴速度低于这个值（m/s）时，这个轮子保持当前朝向、转速为 0（速度为 0 时朝向没有意义） */
#define STEER_MIN_SPEED_M_S 1e-3f

typedef struct
{
    float wheel_radius_m;   /* 轮子半径 */
    float half_wheelbase_m; /* 前后转向轴距离的一半 */
    float half_track_m;     /* 左右转向轴距离的一半 */
} SteerConfig;

typedef struct
{
    float heading_rad; /* 轮子朝向 */
    float speed_rad_s; /* 轮子转速 */
} SteerWheel;

/**
 * @brief   单个舵轮的逆解：转向轴在底盘系 (x_m, y_m) 处的轮子 → 目标朝向和转速
 * @param   heading_rad  这个轮子的当前朝向（可以是多圈的连续值）
 * @note    steer_inverse() 和半舵半全向底盘（half_steer.h）都用它
 */
SteerWheel steer_wheel_inverse(float x_m, float y_m, float wheel_radius_m, const ChassisVel *vel,
                               float heading_rad);

/**
 * @brief   逆解：底盘速度 → 各轮目标朝向和转速
 * @param   heading_rad  各轮当前朝向（可以是多圈的连续值）
 * @pre     各尺寸 > 0
 */
void steer_inverse(const SteerConfig *cfg, const ChassisVel *vel,
                   const float heading_rad[STEER_WHEELS], SteerWheel out[STEER_WHEELS]);

/** 正解：各轮朝向和转速 → 底盘速度；各轮互相矛盾（打滑、朝向没转到位）时取最小二乘解 */
void steer_forward(const SteerConfig *cfg, const SteerWheel wheel[STEER_WHEELS], ChassisVel *vel);

#ifdef __cplusplus
}
#endif
