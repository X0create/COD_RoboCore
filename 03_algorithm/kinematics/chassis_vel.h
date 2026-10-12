/**
 * @file    chassis_vel.h
 * @brief   底盘速度：全向轮、麦轮、舵轮运动学共用的输入输出类型
 * @note    底盘坐标系：原点在底盘几何中心，X 朝车头、Y 朝车左、Z 朝上，从上往下看逆时针转为正。
 *          全仓库的底盘速度都用这一个约定，换轮组时上层代码不用改。
 */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    float vx_m_s;   /* 向前 */
    float vy_m_s;   /* 向左 */
    float wz_rad_s; /* 俯视逆时针 */
} ChassisVel;

#ifdef __cplusplus
}
#endif
