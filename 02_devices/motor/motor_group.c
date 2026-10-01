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
 * 第 1 步：本周期这个电机最终发什么。全部规则只写在这里，按优先级从上往下：
 *   1. 有停机动作（子系统的机构停，或 motor_group_apply_stop_all() 的全车停）→ 执行停机动作
 *   2. 本周期没写力矩指令                                                → 零力矩，不保持上一帧
 *   3. 反馈离线（20 ms 没收到）                                          → 零力矩
 *   4. 否则                                                              → 发写入的力矩
 */
static MotorOutput final_output(const Motor *m)
{
    if (m->safe_set)
    {
        switch (m->safe_action)
        {
            case SAFE_ACTION_ZERO_TORQUE:
                break;
            case SAFE_ACTION_DAMP:
                return (MotorOutput){ .kind = MOTOR_OUT_DAMP };
            case SAFE_ACTION_DISABLE:
                return (MotorOutput){ .kind = MOTOR_OUT_DISABLE };
        }
        return (MotorOutput){ .kind = MOTOR_OUT_ZERO_TORQUE };
    }
    if (!m->torque_set || !watchdog_is_online(&m->wd))
    {
        return (MotorOutput){ .kind = MOTOR_OUT_ZERO_TORQUE };
    }
    return (MotorOutput){ .kind = MOTOR_OUT_TORQUE, .torque_nm = m->torque_cmd_nm };
}

/*
 * 第 2、3 步（DJI）：一条总线上同一控制帧（0x200 / 0x1FF …）的电调共用一帧，每台占 2 字节电流原始值。
 * 力矩以外的输出都发 0；不支持力矩指令的型号（GM6020）量程为 0，结果恒为 0。
 */
static void send_dji_frame(MotorGroup *group, CanBusId bus, uint8_t frame_index)
{
    CanFrame frame = { .id = dji_ctrl_frame_id(frame_index), .len = 8u, .is_fd = false };
    bool used = false;
    bool all_disabled = true;

    for (const Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            continue; /* 达妙每台一帧，见 send_dm_frame */
        }
        uint8_t f, slot;
        dji_ctrl_slot(m->cfg, &f, &slot);
        if (m->cfg->can_bus != bus || f != frame_index)
        {
            continue;
        }
        const int16_t raw =
            m->out.kind == MOTOR_OUT_TORQUE ? dji_torque_to_raw(m->cfg, m->out.torque_nm) : 0;
        used = true;
        all_disabled = all_disabled && m->out.kind == MOTOR_OUT_DISABLE;
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
 * 第 2、3 步（达妙）：每台每个周期必发一帧（驱动器只在收到帧时回反馈，不发就判断不了在不在线）。
 * 使能按“期望状态”对齐：期望与反馈不一致时先发命令（清错 / 使能 / 失能），两条命令至少间隔 DM_CMD_INTERVAL_US；
 * 不发命令的周期按第 1 步的输出发 MIT 帧。
 */
static void send_dm_frame(MotorGroup *group, Motor *m, uint64_t now_us)
{
    DmMotorState *st = &m->brand.dm;
    const MotorConfig *cfg = m->cfg;

    rm_critical_enter();
    const bool enabled = m->fb.enabled;
    const uint8_t error = m->fb.error_code;
    rm_critical_exit();
    const bool online = watchdog_is_online(&m->wd);

    if (!online || m->out.kind == MOTOR_OUT_DISABLE)
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
    else if (m->out.kind == MOTOR_OUT_DAMP)
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, 0.0f, cfg->dm.damp_kd, 0.0f, frame.data);
    }
    else if (m->out.kind == MOTOR_OUT_TORQUE)
    {
        dm_encode_mit(cfg, 0.0f, 0.0f, 0.0f, 0.0f, m->out.torque_nm, frame.data);
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
    /* 1. 确定指令：每个电机本周期最终发什么（停机动作 > 没写指令 > 离线 > 力矩，见 final_output） */
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->out = final_output(m);
    }

    /* 2. 编码 → 3. 发送：DJI 按控制帧打包，达妙每台一帧 */
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        for (uint8_t f = 0u; f < DJI_CTRL_FRAMES; f++)
        {
            send_dji_frame(group, (CanBusId)bus, f);
        }
    }
    const uint64_t now_us = rm_time_now_us();
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        if (m->cfg->type == MOTOR_DM)
        {
            send_dm_frame(group, m, now_us);
        }
    }

    /* 4. 清理：本周期的指令只用一次，下个周期不再写就发零力矩 */
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->torque_set = false;
        m->safe_set = false;
    }
}
