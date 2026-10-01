/**
 * @file    motor_group.c
 * @brief   电机组打包发送，见 motor_group.h
 */
#include "motor_group.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time.h"
#include "dji_motor.h"
#include "dm_motor.h"

void motor_group_apply_stop_all(MotorGroup *group)
{
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        motor_apply_safe_action(m, m->cfg->stop_action);
    }
}

/*
 * 一个电调本周期的电流原始值。
 * DJI 的零力矩和失能都发 0（区别在整帧是否继续发送，见 motor_group_flush）；
 * 没写指令、或反馈离线，填零力矩，不保持上一帧。
 */
static int16_t slot_value(const Motor *m, bool *disabled)
{
    *disabled = m->safe_set && m->safe_action == SAFE_ACTION_DISABLE;
    if (m->safe_set || !m->torque_set || !watchdog_is_online(&m->wd))
    {
        return 0;
    }
    /* 不支持力矩指令的型号（GM6020）量程为 0，结果恒为 0 */
    return dji_torque_to_raw(m->cfg, m->torque_cmd_nm);
}

static void flush_frame(MotorGroup *group, CanBusId bus, uint8_t frame_index)
{
    CanFrame frame = { .id = dji_ctrl_frame_id(frame_index), .len = 8u, .is_fd = false };
    bool used = false;
    bool all_disabled = true;

    for (const Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            continue; /* 达妙每台一帧，见 flush_dm */
        }
        uint8_t f, slot;
        dji_ctrl_slot(m->cfg, &f, &slot);
        if (m->cfg->can_bus != bus || f != frame_index)
        {
            continue;
        }
        bool disabled;
        const int16_t raw = slot_value(m, &disabled);
        used = true;
        all_disabled = all_disabled && disabled;
        frame.data[2u * slot] = (uint8_t)((uint16_t)raw >> 8);
        frame.data[2u * slot + 1u] = (uint8_t)((uint16_t)raw & 0xFFu);
    }

    if (!used)
    {
        return;
    }
    /* 帧里的电调全部失能：发过一次 0 之后就不再发，让电调自己进入无指令状态 */
    if (all_disabled && group->disabled_sent[bus][frame_index])
    {
        return;
    }
    group->disabled_sent[bus][frame_index] = all_disabled;
    if (!can_send(bus, &frame))
    {
        group->tx_dropped++; /* 队列满：丢帧，下个周期重新生成（运行时契约第 6 节） */
    }
}

/*
 * 达妙电机这个周期发哪一帧（每个周期必发一帧：驱动器只在收到帧时回反馈，不发就判断不了在不在线）。
 * 使能按“期望状态”对齐：期望与反馈不一致时发命令，两条命令至少间隔 DM_CMD_INTERVAL_US（等确认或超时）。
 */
static void flush_dm(MotorGroup *group, Motor *m, uint64_t now_us)
{
    DmMotorState *st = &m->brand.dm;
    const MotorConfig *cfg = m->cfg;

    rm_critical_enter();
    const bool enabled = m->fb.enabled;
    const uint8_t error = m->fb.error_code;
    rm_critical_exit();
    const bool online = watchdog_is_online(&m->wd);

    if (!online || (m->safe_set && m->safe_action == SAFE_ACTION_DISABLE))
    {
        st->want_enabled = false; /* 离线后重新上线不自动使能；失能停机同样放弃使能 */
    }

    CanFrame frame = { .id = cfg->id, .len = 8u, .is_fd = can_bus_is_fd(cfg->can_bus) };
    const bool cmd_ready = !st->cmd_sent || now_us - st->last_cmd_us >= DM_CMD_INTERVAL_US;
    bool is_cmd = false;
    if (cmd_ready && online && st->want_enabled && error != 0u && st->clear_requested)
    {
        dm_encode_command(DM_CMD_CLEAR_ERROR, frame.data); /* 这次使能请求只清一次错 */
        st->clear_requested = false;
        is_cmd = true;
    }
    else if (cmd_ready && online && st->want_enabled && !enabled && error == 0u)
    {
        dm_encode_command(DM_CMD_ENABLE, frame.data);
        is_cmd = true;
    }
    else if (cmd_ready && online && !st->want_enabled && enabled)
    {
        dm_encode_command(DM_CMD_DISABLE, frame.data);
        is_cmd = true;
    }

    if (is_cmd)
    {
        st->cmd_sent = true;
        st->last_cmd_us = now_us;
    }
    else if (m->safe_set && m->safe_action == SAFE_ACTION_DAMP)
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, 0.0f, cfg->dm.damp_kd, 0.0f, frame.data);
    }
    else if (!m->safe_set && m->torque_set && online)
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, 0.0f, 0.0f, m->torque_cmd_nm, frame.data);
    }
    else
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, frame.data); /* 零力矩：Kp、Kd、力矩全 0 */
    }

    if (!can_send(cfg->can_bus, &frame))
    {
        group->tx_dropped++;
    }
}

void motor_group_flush(MotorGroup *group)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        for (uint8_t f = 0u; f < DJI_CTRL_FRAMES; f++)
        {
            flush_frame(group, (CanBusId)bus, f);
        }
    }
    const uint64_t now_us = rm_time_now_us();
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            flush_dm(group, m, now_us);
        }
    }
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->torque_set = false;
        m->safe_set = false;
    }
}
