/**
 * @file    dji_motor.c
 * @brief   DJI 电机协议，见 dji_motor.h
 * @note    反馈字节布局照 COD-H7-Template `Motor.c` 的 DJI_Motor_Info_Update；单位换算为本模板新增（ADR 0031）。
 */
#include "dji_motor.h"

#define ENCODER_COUNTS 8192
#define ENCODER_HALF   4096
#define RPM_TO_RAD_S   (RM_TWO_PI / 60.0f)
#define RAD_PER_COUNT  (RM_TWO_PI / (float)ENCODER_COUNTS)

/*
 * 每种型号一行常数。力矩常数折算到转子（输出轴常数 ÷ 原装减速比），这样拆掉减速箱时只改 gear_ratio。
 * - M3508 / C620：电流 ±16384 对应 ±20 A；输出轴 0.3 N·m/A（原装 3591/187）。引用 basic_framework，附录 A.2
 * - M2006 / C610：电流 ±10000 对应 ±10 A；输出轴 0.18 N·m/A（原装 36:1）。待对照大疆手册核对
 * - GM6020：反馈电流 ±16384 对应 ±3 A、0.741 N·m/A 为待核对值；本步不发指令（ADR 0031）
 */
typedef struct
{
    float amps_per_raw;  /* 电流原始值 → A（反馈和指令共用） */
    int16_t cmd_max_raw; /* 电流指令量程；0 表示不支持力矩指令 */
    float kt_rotor;      /* 转子力矩常数 N·m/A */
    bool has_temperature;
} DjiTypeParams;

static const DjiTypeParams type_params[] = {
    [MOTOR_M3508] = { DJI_C620_MAX_A / (float)DJI_C620_RAW_MAX, DJI_C620_RAW_MAX,
                      DJI_M3508_NM_PER_A / DJI_M3508_GEAR_RATIO, true },
    [MOTOR_M2006] = { 10.0f / 10000.0f, 10000, 0.18f / DJI_M2006_GEAR_RATIO, false },
    [MOTOR_GM6020] = { 3.0f / 16384.0f, 0, 0.741f, true },
};

static bool is_c6x0(MotorType type)
{
    return type == MOTOR_M3508 || type == MOTOR_M2006;
}

bool dji_config_valid(const MotorConfig *cfg)
{
    const uint8_t max_id = is_c6x0(cfg->type) ? 8u : 7u;
    return cfg->id >= 1u && cfg->id <= max_id && (cfg->direction == 1 || cfg->direction == -1)
           && cfg->gear_ratio > 0.0f
           && (cfg->stop_action == SAFE_ACTION_ZERO_TORQUE
               || cfg->stop_action == SAFE_ACTION_DISABLE);
}

uint32_t dji_feedback_id(const MotorConfig *cfg)
{
    return (is_c6x0(cfg->type) ? 0x200u : 0x204u) + cfg->id;
}

void dji_ctrl_slot(const MotorConfig *cfg, uint8_t *frame, uint8_t *slot)
{
    /* C6x0：1–4 号在 0x200、5–8 号在 0x1FF；GM6020 电压：1–4 号在 0x1FF、5–7 号在 0x2FF */
    const uint8_t index = (uint8_t)(cfg->id - 1u); /* 0–7 */
    const uint8_t first_frame = is_c6x0(cfg->type) ? 0u : 1u;
    *frame = (uint8_t)(first_frame + index / 4u);
    *slot = (uint8_t)(index % 4u);
}

uint32_t dji_ctrl_frame_id(uint8_t frame)
{
    static const uint32_t ids[] = { 0x200u, 0x1FFu, 0x2FFu };
    return ids[frame];
}

MotorCaps dji_caps(MotorType type)
{
    return (MotorCaps){ .torque_command = type_params[type].cmd_max_raw > 0,
                        .torque_feedback_exact = false,
                        .needs_enable = false };
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

void dji_decode_feedback(const MotorConfig *cfg, DjiMotorState *state, const uint8_t data[8],
                         MotorFeedback *out)
{
    const DjiTypeParams *tp = &type_params[cfg->type];
    const uint16_t encoder = (uint16_t)be16(&data[0]) % ENCODER_COUNTS;
    const int16_t rpm = be16(&data[2]);
    const int16_t current_raw = be16(&data[4]);
    const float dir = (float)cfg->direction;

    if (!state->have_last)
    {
        /* 带减速箱的型号上电位置未知，以第一帧为 0；GM6020 直驱，编码器就是输出轴位置 */
        state->zero_encoder = is_c6x0(cfg->type) ? encoder : 0u;
        state->turns = 0;
        state->have_last = true;
    }
    else
    {
        /* 相邻两帧转子最多转半圈（M3508 空载约 9000 rpm，1 ms 转 0.15 圈），跨过半圈就是过零 */
        const int32_t delta = (int32_t)encoder - (int32_t)state->last_encoder;
        if (delta > ENCODER_HALF)
        {
            state->turns--;
        }
        else if (delta < -ENCODER_HALF)
        {
            state->turns++;
        }
    }
    state->last_encoder = encoder;

    /* 圈数和圈内计数分开换算成 float，不做累加，长时间运行不丢精度 */
    const float rotor_rad =
        (float)state->turns * RM_TWO_PI
        + (float)((int32_t)encoder - (int32_t)state->zero_encoder) * RAD_PER_COUNT;
    const int32_t signed_count =
        (encoder >= ENCODER_HALF) ? (int32_t)encoder - ENCODER_COUNTS : encoder;

    out->angle_rad = dir * rotor_rad / cfg->gear_ratio;
    out->single_angle_rad = (float)signed_count * RAD_PER_COUNT;
    out->raw_encoder = encoder;
    out->speed_rad_s = dir * (float)rpm * RPM_TO_RAD_S / cfg->gear_ratio;
    out->torque_nm = dir * (float)current_raw * tp->amps_per_raw * tp->kt_rotor * cfg->gear_ratio;
    out->torque_is_estimate = true;
    out->temperature_c = tp->has_temperature ? (float)data[6] : 0.0f;
    out->error_code = 0u;
    out->enabled = true; /* DJI 电调没有使能概念 */
}

int16_t dji_torque_to_raw(const MotorConfig *cfg, float torque_nm)
{
    const DjiTypeParams *tp = &type_params[cfg->type];
    const float max = (float)tp->cmd_max_raw;
    float raw =
        (float)cfg->direction * torque_nm / (tp->kt_rotor * cfg->gear_ratio) / tp->amps_per_raw;
    if (raw > max)
    {
        raw = max;
    }
    else if (raw < -max)
    {
        raw = -max;
    }
    return (int16_t)(raw >= 0.0f ? raw + 0.5f : raw - 0.5f);
}
