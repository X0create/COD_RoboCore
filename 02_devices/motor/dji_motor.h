/**
 * @file    dji_motor.h
 * @brief   DJI 电机（M3508 / M2006 / GM6020）的协议细节，只给 02_devices/motor/ 内部用
 * @note    子系统不要 include 本文件，只用 motor.h。协议见《架构设计》附录 A.2：
 *          反馈帧 8 字节大端：编码器 0–8191、转速 rpm、电流原始值、温度；控制帧每个电调 2 字节大端 int16。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motor.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 配置是否支持：ID 范围、方向、减速比、停机动作 */
bool dji_config_valid(const MotorConfig *cfg);

/** 反馈帧 ID：M3508 / M2006 为 0x200 + id，GM6020 为 0x204 + id */
uint32_t dji_feedback_id(const MotorConfig *cfg);

/**
 * @brief   这个电调在哪个控制帧的第几个槽位
 * @param   frame  控制帧序号 0–2，对应 dji_ctrl_frame_id()
 * @param   slot   帧内槽位 0–3
 * @note    GM6020 目前不发指令，但仍占它的电压帧槽位：同一路上 GM6020 1–4 号与 C6x0 5–8 号共用 0x1FF，
 *          给 C6x0 发帧时会把 GM6020 的槽位写成 0，必须当作冲突拒绝
 */
void dji_ctrl_slot(const MotorConfig *cfg, uint8_t *frame, uint8_t *slot);

/** 控制帧序号 → CAN ID（0x200、0x1FF、0x2FF） */
uint32_t dji_ctrl_frame_id(uint8_t frame);

MotorCaps dji_caps(MotorType type);

/**
 * @brief   解析一帧反馈，更新多圈计数，得到输出轴上的国际单位（纯计算）
 * @param   data  8 字节反馈数据
 * @note    out 的 online 不在这里填
 */
void dji_decode_feedback(const MotorConfig *cfg, DjiMotorState *state, const uint8_t data[8],
                         MotorFeedback *out);

/** 输出轴力矩 → 电调电流原始值（截到量程，四舍五入） @pre dji_caps(type).torque_command */
int16_t dji_torque_to_raw(const MotorConfig *cfg, float torque_nm);

#ifdef __cplusplus
}
#endif
