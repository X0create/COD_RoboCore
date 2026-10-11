/**
 * @file    pid.h
 * @brief   位置式 / 增量式 PID（纯计算，电脑上可测）
 * @note    移植自 COD-H7-Template `Components/Controller/Src/PID.c`，计算方式不变：
 *          - 不含 dt：积分是误差逐次累加，微分是相邻两次误差之差，所以参数和调用频率绑定
 *            （旧工程底盘速度环 1 kHz、加热环 5 ms 调一次）；换调用频率要重新整定。
 *          - 误差绝对值小于死区时不计算，输出保持上一次的值。
 *          - 微分项可以过一阶低通（0 < d_alpha < 1 时启用）。
 *          - 积分限幅按**误差累加值**计，不是按输出单位；integral_limit 为 0 时积分项恒为 0。
 *          - 抗积分饱和（ADR 0040，旧工程没有）：位置式的积分只累加到“输出刚好等于 output_limit”为止，
 *            误差还在往同一方向推时不再多攒。否则预热、堵转这类长时间饱和会把积分攒满，目标到了还要等积分慢慢退回来（过冲）。
 *            增量式的输出本身有限幅、不会越攒越多，不需要。
 *          与旧工程的差异见 docs/CHANGES_FROM_COD_H7_TEMPLATE.md。
 */
#pragma once

#include "03_algorithm/filter/lpf.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    PID_POSITION, /* 位置式：输出 = P + I + D */
    PID_INCREMENTAL, /* 增量式：每次在上一次输出上加 ΔP + ΔI + ΔD（旧工程叫 PID_VELOCITY） */
} PidType;

typedef struct
{
    float kp;
    float ki;
    float kd;
    float d_alpha;        /* 微分低通系数，0 < d_alpha < 1 时启用，其余值不滤波 */
    float deadband;       /* |误差| < deadband 时输出保持不变 */
    float integral_limit; /* 误差累加值的限幅（只用于位置式） */
    float output_limit;   /* 输出限幅 */
} PidParam;

/* 各中间量都保留在结构体里，方便在 Ozone 里看曲线、调参数 */
typedef struct
{
    PidType type;
    PidParam param;
    float target;
    float measure;
    float err[3]; /* 本次、上次、上上次的误差 */
    float integral;
    float p_out;
    float i_out;
    float d_out;
    float output;
    Lpf1 d_lpf;
} Pid;

/** @pre param 各限幅 >= 0 */
void pid_init(Pid *pid, PidType type, const PidParam *param);

/** 计算一次，返回输出（同时存在 pid->output） */
float pid_step(Pid *pid, float target, float measure);

/** 清零误差历史、积分、各项输出和微分滤波器状态，参数不变。机构停下或切换模式时调用，防止恢复时猛冲 */
void pid_reset(Pid *pid);

#ifdef __cplusplus
}
#endif
