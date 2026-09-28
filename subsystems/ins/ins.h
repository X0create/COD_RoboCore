/**
 * @file    ins.h
 * @brief   惯性导航子系统：读 BMI088 → 上电零偏标定 → 安装旋转 → 加速度低通 → 四元数 EKF → 发布 ImuState
 * @note    移植自 COD-H7-Template `INS_Task.c`（EKF 参数、加速度二阶低通系数、多圈航向不变）。与旧工程的差异：
 *          - 上电静止标定陀螺零偏（ADR 0033）：采 INS_CALIB_SAMPLES 个样本，标准差和均值都在阈值内才采用，
 *            否则报告原因并重新采样；标定完成前不发布 ImuState，安全门据此全车停；
 *          - 芯片坐标系用兵种配置的安装旋转转到机体系（ADR 0006），旧工程用欧拉角下标重映射；
 *          - 加速度模长接近 0 的读数当作坏帧丢弃（全零会让 EKF 除零后永久变成 NaN）；
 *          - 读失败时关加热、不发布。
 *          本模块不打日志：ins_step() 返回事件，由调用它的任务记录。
 *          轮询驱动：每 1 ms 调用一次 ins_step()（ADR 0034，同旧工程；数据就绪中断以后再加）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "algorithm/attitude/gyro_bias.h"
#include "algorithm/attitude/quat_ekf.h"
#include "algorithm/filter/lpf.h"
#include "devices/imu/bmi088.h"
#include "msgs/imu_state.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 上电标定（UniC 实测值：静止噪声约 0.013 rad/s） */
#define INS_CALIB_SAMPLES  2000u /* 1 kHz 下 2 s */
#define INS_CALIB_MAX_STD  0.05f /* rad/s，约 3.8 倍静止噪声；碰一下（≥ 0.1 rad/s）就会拒绝 */
#define INS_CALIB_MAX_BIAS 0.15f /* rad/s，BMI088 零偏远小于 0.1；更大说明在匀速转 */

typedef struct
{
    /* 安装旋转：机体系向量 = R × 芯片系向量，按行存储；芯片与机体同向时为单位阵 */
    float install_rotation[9];
} InsConfig;

typedef enum
{
    INS_PHASE_CALIBRATING,
    INS_PHASE_RUNNING,
} InsPhase;

typedef enum
{
    INS_EVENT_NONE,
    INS_EVENT_READ_FAILED,          /* 这次没读到有效数据（加热已关） */
    INS_EVENT_CALIBRATED,           /* 标定完成，开始发布 */
    INS_EVENT_CALIB_NOT_STILL,      /* 标定被拒：在动，重新采样 */
    INS_EVENT_CALIB_BIAS_TOO_LARGE, /* 标定被拒：均值过大，重新采样 */
} InsEvent;

typedef struct
{
    const InsConfig *cfg;
    ImuStateTopic *out;
    Bmi088 imu;
    InsPhase phase;
    GyroBias calib;
    QuatEkf ekf;
    Lpf2 accel_lpf[3];
    bool have_yaw;
    float last_yaw_rad;
    int32_t yaw_turns;
    uint32_t read_failures; /* 调试用 */
} Ins;

/**
 * @brief   认领 ImuState 话题，初始化滤波器和 EKF
 * @return  false：话题已被别的模块认领
 * @pre     初始化阶段调用
 */
RM_NODISCARD bool ins_init(Ins *ins, const InsConfig *cfg, ImuStateTopic *out);

/** 初始化 BMI088（阻塞约 170 ms，在 ins 任务里调用），失败返回原因，调用者稍后重试 */
Bmi088Status ins_start(Ins *ins);

/** 每 1 ms 调用一次 @pre ins_start() 已返回 BMI088_OK */
InsEvent ins_step(Ins *ins);

#ifdef __cplusplus
}
#endif
