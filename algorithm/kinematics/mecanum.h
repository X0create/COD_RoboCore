/**
 * @file    mecanum.h
 * @brief   四轮麦克纳姆轮底盘运动学：底盘速度 ↔ 各轮转速（纯计算，电脑上可测）
 * @note    几何约定（俯视，底盘系 C）：
 *          - 轮 0–3 = 左前、左后、右后、右前（逆时针，与 omni.h 的 X 形布置同序），接地点在
 *            (±half_wheelbase_m, ±half_track_m)；
 *          - 轮子正转 = 这个轮子把底盘往前推。电机输出轴正方向要用 MotorConfig.direction 调成与此一致；
 *          - 辊子按常见的 O 形安装：从上往下看，四个轮子上方的辊子围成 O 形（接地辊子的轴线都指向
 *            底盘中心）。此时向左平移，左前、右后轮反转，左后、右前轮正转。装成 X 形时不能正常横移、
 *            旋转，上板第一次横移就能看出来。
 *          设 k = half_wheelbase_m + half_track_m：
 *            ω_0 = (vx − vy − k·wz) / r，ω_1 = (vx + vy − k·wz) / r，
 *            ω_2 = (vx − vy + k·wz) / r，ω_3 = (vx + vy + k·wz) / r。
 */
#pragma once

#include "chassis_vel.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MECANUM_WHEELS 4u

typedef struct
{
    float wheel_radius_m;   /* 麦轮半径 */
    float half_wheelbase_m; /* 前后轮轴距的一半 */
    float half_track_m;     /* 左右轮距的一半 */
} MecanumConfig;

/** 逆解：底盘速度 → 各轮转速（rad/s） @pre 各尺寸 > 0 */
void mecanum_inverse(const MecanumConfig *cfg, const ChassisVel *vel,
                     float wheel_rad_s[MECANUM_WHEELS]);

/** 正解：各轮转速 → 底盘速度；四个轮速互相矛盾（打滑）时取最小二乘解 */
void mecanum_forward(const MecanumConfig *cfg, const float wheel_rad_s[MECANUM_WHEELS],
                     ChassisVel *vel);

#ifdef __cplusplus
}
#endif
