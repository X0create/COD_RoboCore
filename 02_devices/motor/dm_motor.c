/**
 * @file    dm_motor.c
 * @brief   达妙电机协议，见 dm_motor.h
 */
#include "dm_motor.h"

/*
 * 配置检查：CAN ID 1–15、方向 ±1；达妙驱动器反馈的就是输出轴，所以减速比必须是 1；
 * 位置 / 速度 / 力矩量程（p_max、v_max、t_max）要和驱动器上位机里设置的一致，否则换算全错；
 * Master ID（反馈帧 ID）不能和 CAN ID 相同
 */
bool dm_config_valid(const MotorConfig *cfg)
{
    const DmConfig *dm = &cfg->dm;
    return cfg->id >= 1u && cfg->id <= 0x0Fu && (cfg->direction == 1 || cfg->direction == -1)
           && cfg->gear_ratio == 1.0f && dm->p_max > 0.0f && dm->v_max > 0.0f && dm->t_max > 0.0f
           && dm->damp_kd >= 0.0f && dm->damp_kd <= DM_KD_MAX && dm->master_id >= 1u
           && dm->master_id <= CAN_STD_ID_MAX && dm->master_id != cfg->id;
}

MotorCaps dm_caps(void)
{
    return (
        MotorCaps){ .torque_command = true, .torque_feedback_exact = false, .needs_enable = true };
}

/* 整数 → 浮点：[0, 2^bits − 1] 线性映射到 [lo, hi]（旧代码 uint_to_float） */
static float uint_to_float(uint32_t x, float lo, float hi, uint32_t bits)
{
    return (float)x * (hi - lo) / (float)((1u << bits) - 1u) + lo;
}

/* 浮点 → 整数：旧代码 float_to_uint 的换算（截尾取整），加了范围截断 */
static uint32_t float_to_uint(float x, float lo, float hi, uint32_t bits)
{
    x = (x < lo) ? lo : ((x > hi) ? hi : x);
    return (uint32_t)((x - lo) * (float)((1u << bits) - 1u) / (hi - lo));
}

void dm_decode_feedback(const MotorConfig *cfg, const uint8_t data[8], MotorFeedback *out)
{
    /* 反馈帧：[0] 高 4 位状态、低 4 位 ID | [1–2] 位置 16 位 | [3–4 高] 速度 12 位 | [4 低–5] 力矩 12 位 | [6] MOS 温度 | [7] 线圈温度 */
    const DmConfig *dm = &cfg->dm;
    const float dir = (float)cfg->direction;
    const uint8_t state = data[0] >> 4;
    const uint32_t p = ((uint32_t)data[1] << 8) | data[2];
    const uint32_t v = ((uint32_t)data[3] << 4) | (data[4] >> 4);
    const uint32_t t = ((uint32_t)(data[4] & 0x0Fu) << 8) | data[5];
    const float pos = uint_to_float(p, -dm->p_max, dm->p_max, 16u);

    out->angle_rad = dir * pos; /* 驱动器给出的就是输出轴位置，范围 ±p_max */
    out->single_angle_rad = pos;
    out->raw_encoder = (uint16_t)p;
    out->speed_rad_s = dir * uint_to_float(v, -dm->v_max, dm->v_max, 12u);
    out->torque_nm = dir * uint_to_float(t, -dm->t_max, dm->t_max, 12u);
    out->torque_is_estimate = true;
    out->temperature_c = (float)data[7]; /* 线圈温度（data[6] 是 MOS 温度） */
    out->error_code = (state >= DM_STATE_ERROR_MIN) ? state : 0u;
    out->enabled = state == DM_STATE_ENABLED;
}

void dm_encode_mit(const MotorConfig *cfg, float pos_rad, float vel_rad_s, float kp, float kd,
                   float torque_nm, uint8_t out[8])
{
    const DmConfig *dm = &cfg->dm;
    const float dir = (float)cfg->direction;
    const uint32_t p = float_to_uint(dir * pos_rad, -dm->p_max, dm->p_max, 16u);
    const uint32_t v = float_to_uint(dir * vel_rad_s, -dm->v_max, dm->v_max, 12u);
    const uint32_t t = float_to_uint(dir * torque_nm, -dm->t_max, dm->t_max, 12u);
    const uint32_t kp_u = float_to_uint(kp, 0.0f, DM_KP_MAX, 12u);
    const uint32_t kd_u = float_to_uint(kd, 0.0f, DM_KD_MAX, 12u);

    /* MIT 帧：位置 16 位 | 速度 12 位 | Kp 12 位 | Kd 12 位 | 力矩 12 位，共 64 位，按位紧挨着排 */
    out[0] = (uint8_t)(p >> 8);
    out[1] = (uint8_t)p;
    out[2] = (uint8_t)(v >> 4);
    out[3] = (uint8_t)(((v & 0x0Fu) << 4) | (kp_u >> 8));
    out[4] = (uint8_t)kp_u;
    out[5] = (uint8_t)(kd_u >> 4);
    out[6] = (uint8_t)(((kd_u & 0x0Fu) << 4) | (t >> 8));
    out[7] = (uint8_t)t;
}

/* 命令帧：前 7 字节 0xFF，最后一字节是命令（使能 0xFC、失能 0xFD、清错 0xFB …，见 DmCommand） */
void dm_encode_command(DmCommand cmd, uint8_t out[8])
{
    for (int i = 0; i < 7; i++)
    {
        out[i] = 0xFFu;
    }
    out[7] = (uint8_t)cmd;
}
