/**
 * @file    chassis_vel.h
 * @brief   底盘速度：全向轮、麦轮、舵轮运动学共用的输入输出类型
 * @note    底盘系 C（《架构设计》约定表）：原点在底盘几何中心，X 车头、Y 左、Z 上，俯视逆时针为正。
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
