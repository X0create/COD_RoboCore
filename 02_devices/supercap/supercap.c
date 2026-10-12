/**
 * @file    supercap.c
 * @brief   超级电容通信，见 supercap.h；帧格式见 README.md。从上往下：编解码 → 初始化 → 收 → 读 → 发
 */
#include "02_devices/supercap/supercap.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time/time.h"

#define FRAME_LEN 8u

/* 分辨率：1 个单位代表多少 */
#define POWER_LSB_W     0.1f  /* 功率 0.1 W */
#define ENERGY_LSB_J    0.1f  /* 能量 0.1 J */
#define VOLTAGE_LSB_V   0.01f /* 电压 0.01 V */
#define ENERGY_FULL_RAW 255.0f

#define CMD_FLAG_ENABLE      0x01u /* 指令第 0 字节第 0 位：允许输出 */
#define FEEDBACK_FLAG_OUTPUT 0x01u /* 反馈第 1 字节第 0 位：正在输出 */

/* ================================================================== */
/* 编解码：统一小端                                                    */
/* ================================================================== */

/* 浮点 → 无符号 16 位：按分辨率四舍五入，负数按 0，超出 65535 截到 65535 */
static uint16_t to_u16(float value, float lsb)
{
    const float raw = value / lsb + 0.5f;
    if (!(raw > 0.0f)) /* 负数和 NaN 都按 0 */
    {
        return 0u;
    }
    return raw >= 65535.0f ? 65535u : (uint16_t)raw;
}

void supercap_encode_cmd(const SupercapCmd *cmd, uint8_t out[8])
{
    const uint16_t power = to_u16(cmd->power_limit_w, POWER_LSB_W);
    const uint16_t energy = to_u16(cmd->buffer_energy_j, ENERGY_LSB_J);
    out[0] = cmd->enable_output ? CMD_FLAG_ENABLE : 0u;
    out[1] = (uint8_t)(power & 0xFFu);
    out[2] = (uint8_t)(power >> 8);
    out[3] = (uint8_t)(energy & 0xFFu);
    out[4] = (uint8_t)(energy >> 8);
    out[5] = SUPERCAP_PROTOCOL_VERSION;
    out[6] = 0u; /* 保留 */
    out[7] = 0u;
}

bool supercap_decode_feedback(const uint8_t data[8], SupercapFeedback *out)
{
    if (data[7] != SUPERCAP_PROTOCOL_VERSION)
    {
        return false;
    }
    const uint16_t voltage = (uint16_t)(data[2] | (data[3] << 8));
    const int16_t power = (int16_t)(uint16_t)(data[4] | (data[5] << 8)); /* 有符号：回馈时为负 */
    *out = (SupercapFeedback){ .error_flags = data[0],
                               .output_enabled = (data[1] & FEEDBACK_FLAG_OUTPUT) != 0u,
                               .cap_voltage_v = (float)voltage * VOLTAGE_LSB_V,
                               .chassis_power_w = (float)power * POWER_LSB_W,
                               .energy_ratio = (float)data[6] / ENERGY_FULL_RAW };
    return true;
}

/* ================================================================== */
/* 初始化                                                              */
/* ================================================================== */

SupercapInitResult supercap_init(Supercap *cap, const SupercapConfig *cfg, const MotorGroup *motors)
{
    if (cfg->cmd_id > CAN_STD_ID_MAX || cfg->feedback_id > CAN_STD_ID_MAX
        || cfg->cmd_id == cfg->feedback_id)
    {
        return SUPERCAP_INIT_BAD_CONFIG;
    }
    if (can_bus_is_fd(cfg->can_bus))
    {
        return SUPERCAP_INIT_FD_BUS;
    }
    /* 同一个 ID 被电机用着：指令会被电调当成控制帧，反馈会和电机反馈混在一起 */
    if (motor_group_find_can_id(motors, cfg->can_bus, cfg->cmd_id) != NULL
        || motor_group_find_can_id(motors, cfg->can_bus, cfg->feedback_id) != NULL)
    {
        return SUPERCAP_INIT_MOTOR_CONFLICT;
    }
    *cap = (Supercap){ .cfg = cfg };
    watchdog_register(&cap->wd, cfg->name, SUPERCAP_TIMEOUT_MS);
    return SUPERCAP_INIT_OK;
}

/* ================================================================== */
/* 收：comm_rx_task                                                    */
/* ================================================================== */

/* 是它的 → 检查长度和版本 → 解码 → 和接收时刻一起存下（同一个临界区，读到的数据和在线状态对得上） */
bool supercap_receive(Supercap *cap, CanBusId bus, const CanFrame *frame)
{
    if (bus != cap->cfg->can_bus || frame->id != cap->cfg->feedback_id)
    {
        return false;
    }
    SupercapFeedback fb;
    if (frame->len != FRAME_LEN || !supercap_decode_feedback(frame->data, &fb))
    {
        cap->bad_frames++; /* 长度或版本不对：两边协议不一致，丢弃、不喂狗 */
        return true;
    }
    rm_critical_enter();
    cap->fb = fb;
    watchdog_feed(&cap->wd,
                  frame->stamp_us); /* 用中断里记下的收帧时刻，积压的旧帧不会被当成刚收到的 */
    rm_critical_exit();
    return true;
}

/* ================================================================== */
/* 读：使用方任务                                                      */
/* ================================================================== */

bool supercap_read(const Supercap *cap, SupercapFeedback *out)
{
    rm_critical_enter();
    *out = cap->fb;
    const bool online = watchdog_is_online(&cap->wd);
    rm_critical_exit();
    return online;
}

/* ================================================================== */
/* 发：control_task                                                    */
/* ================================================================== */

void supercap_send(Supercap *cap, const SupercapCmd *cmd)
{
    const uint64_t now_us = rm_time_now_us();
    if (cap->cmd_sent && now_us - cap->last_cmd_us < (uint64_t)SUPERCAP_CMD_PERIOD_MS * 1000u)
    {
        return; /* 还没到发送时间 */
    }
    cap->cmd_sent = true;
    cap->last_cmd_us = now_us;

    CanFrame frame = { .id = cap->cfg->cmd_id, .len = FRAME_LEN, .is_fd = false };
    supercap_encode_cmd(cmd, frame.data);
    if (!can_send(cap->cfg->can_bus, &frame))
    {
        cap->tx_dropped++; /* 队列满：丢这一帧，下个周期发新的（电容板 100 ms 内收到一帧就不会停） */
    }
}
