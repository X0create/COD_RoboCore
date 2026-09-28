/**
 * @file    motor_group.c
 * @brief   电机组打包发送，见 motor_group.h
 */
#include "motor_group.h"

#include <stddef.h>

#include "dji_motor.h"

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

void motor_group_flush(MotorGroup *group)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        for (uint8_t f = 0u; f < DJI_CTRL_FRAMES; f++)
        {
            flush_frame(group, (CanBusId)bus, f);
        }
    }
    for (Motor *m = group->head; m != NULL; m = m->next)
    {
        m->torque_set = false;
        m->safe_set = false;
    }
}
