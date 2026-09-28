/**
 * @file    imu_state.h
 * @brief   姿态与惯性测量消息（ins 发布）
 * @note    ins 只在零偏标定完成后发布，所以“读得到且不旧”就是“IMU 就绪”：
 *          安全门用 imu_state_read(topic, &s, IMU_STALE_MS) 判断，失败即全车停（运行时契约第 5 节）。
 *          坐标系为 IMU 所属刚体的机体系（X 前、Y 左、Z 上，ADR 0006），由兵种配置的安装旋转转换。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/msg/topic.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 超过这么久没有新姿态就算 IMU 未就绪（ins 1 kHz，20 ms 即连续 20 次没有更新） */
#define IMU_STALE_MS 20u

typedef struct
{
    float q[4]; /* 机体系 → 世界系，[w x y z]，已归一化 */
    /* ZYX 欧拉角，只用于显示和调试，控制用四元数（《架构设计》坐标系约定）。
     * 都是绕对应轴右手为正：yaw 从上往下看逆时针为正；pitch 绕 +Y（朝左）为正，即**低头为正**，
     * 与云台“抬头为正”的约定（ADR 0006）相反；roll 绕 +X（朝前）为正，即左侧抬起为正 */
    float yaw_rad;
    float pitch_rad;
    float roll_rad;
    float yaw_total_rad; /* 多圈航向（不回绕） */
    float gyro_rad_s[3]; /* 机体系角速度，已减上电标定的零偏 */
    float accel_m_s2[3]; /* 机体系加速度，二阶低通后 */
    float temperature_c; /* IMU 芯片温度 */
} ImuState;

_Static_assert(sizeof(ImuState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

typedef struct
{
    Topic base;
    ImuState data;
} ImuStateTopic;

RM_NODISCARD bool imu_state_claim(ImuStateTopic *topic, const char *owner);
void imu_state_publish(ImuStateTopic *topic, const ImuState *state);
/** @return false：从未发布或比 max_age_ms 更旧，out 不被修改 */
RM_NODISCARD bool imu_state_read(const ImuStateTopic *topic, ImuState *out, uint32_t max_age_ms);

#ifdef __cplusplus
}
#endif
