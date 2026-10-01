/**
 * @file    dm_motor.h
 * @brief   达妙电机（MIT 模式）的协议细节，只给 02_devices/motor/ 内部用
 * @note    子系统不要 include 本文件，只用 motor.h。协议见《架构设计》附录 A.3：
 *          - MIT 帧（ID = 电机 CAN ID）：位置 16 位、速度 12 位、Kp 12 位、Kd 12 位、力矩 12 位，按范围线性映射；
 *          - 命令帧（ID 同上）：前 7 字节 0xFF，最后一字节 0xFC 使能、0xFD 失能、0xFB 清错；
 *            旧工程把命令发到反馈 ID，与参考实现不符，这里按协议发到电机 CAN ID（ADR 0035）；
 *          - 反馈帧（ID = Master ID）：第 0 字节高 4 位状态、低 4 位 ID，之后位置、速度、力矩、MOS 温度、线圈温度。
 *          字节布局照 COD-H7-Template `Motor.c`；浮点 → 整数加了截断（旧代码超出范围时会绕回，负力矩可能变成最大正力矩）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motor.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DM_KP_MAX 500.0f /* MIT 帧 Kp 范围 0–500（协议固定） */
#define DM_KD_MAX 5.0f   /* MIT 帧 Kd 范围 0–5 */

/** 两条命令之间至少间隔这么久：等反馈确认，没确认就在这之后重发（运行时契约第 6 节） */
#define DM_CMD_INTERVAL_US 20000u

typedef enum
{
    DM_CMD_ENABLE = 0xFC,
    DM_CMD_DISABLE = 0xFD,
    DM_CMD_CLEAR_ERROR = 0xFB,
} DmCommand;

/** 状态码（反馈第 0 字节高 4 位） */
#define DM_STATE_DISABLED  0x0u
#define DM_STATE_ENABLED   0x1u
#define DM_STATE_ERROR_MIN 0x8u /* 0x8 过压 … 0xD 通信丢失、0xE 过载 */

/** 配置是否支持：ID 1–15、方向 ±1、减速比 1、范围为正、阻尼 Kd 在 0–5、Master ID 与 CAN ID 不同 */
bool dm_config_valid(const MotorConfig *cfg);

MotorCaps dm_caps(void);

/** 解析反馈帧 @note out 的 online 不在这里填 */
void dm_decode_feedback(const MotorConfig *cfg, const uint8_t data[8], MotorFeedback *out);

/** 编码 MIT 帧（输出轴单位，乘方向；各量截到范围内） */
void dm_encode_mit(const MotorConfig *cfg, float pos_rad, float vel_rad_s, float kp, float kd,
                   float torque_nm, uint8_t out[8]);

void dm_encode_command(DmCommand cmd, uint8_t out[8]);

#ifdef __cplusplus
}
#endif
