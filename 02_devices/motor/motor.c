/**
 * @file    motor.c
 * @brief   电机统一接口，见 motor.h
 * @note    按品牌分派只在本文件和 motor_group.c 里。
 */
#include "motor.h"

#include <stddef.h>

#include "04_core/os/critical.h"
#include "05_platform/time.h"
#include "dji_motor.h"
#include "dm_motor.h"
#include "motor_group.h"

static bool is_dm(const MotorConfig *cfg)
{
    return cfg->type == MOTOR_DM;
}

static uint32_t feedback_id(const MotorConfig *cfg)
{
    return is_dm(cfg) ? cfg->dm.master_id : dji_feedback_id(cfg);
}

bool motor_receive(Motor *m, CanBusId bus, const CanFrame *frame)
{
    if (bus != m->cfg->can_bus || frame->id != feedback_id(m->cfg))
    {
        return false; /* 不是这个电机的反馈 */
    }
    if (frame->len != 8u)
    {
        return true; /* 是它的 ID 但长度不对：丢弃、不喂狗 */
    }

    MotorFeedback fb;
    if (is_dm(m->cfg))
    {
        dm_decode_feedback(m->cfg, frame->data, &fb);
    }
    else
    {
        dji_decode_feedback(m->cfg, &m->brand.dji, frame->data, &fb);
    }
    fb.online = false; /* 在线与否由 motor_read_feedback() 读取时计算 */
    fb.stamp_us = rm_time_now_us();

    rm_critical_enter();
    m->fb = fb;
    rm_critical_exit();
    watchdog_feed(&m->wd);
    return true;
}

/* 这个电机在总线上占用的 ID：DJI 为反馈 ID 和控制帧 ID，达妙为 CAN ID 和 Master ID */
static void bus_ids(const MotorConfig *cfg, uint32_t ids[2])
{
    if (is_dm(cfg))
    {
        ids[0] = cfg->id;
        ids[1] = cfg->dm.master_id;
        return;
    }
    uint8_t frame, slot;
    dji_ctrl_slot(cfg, &frame, &slot);
    ids[0] = dji_feedback_id(cfg);
    ids[1] = dji_ctrl_frame_id(frame);
}

/*
 * 同一路 CAN 上的冲突：
 * - 两个 DJI：反馈 ID 相同，或占同一个控制帧槽位（它们可以共用一个控制帧）；
 * - 其余组合：占用的 ID 有重合。
 */
static bool conflicts(const MotorConfig *a, const MotorConfig *b)
{
    if (a->can_bus != b->can_bus)
    {
        return false;
    }
    if (!is_dm(a) && !is_dm(b))
    {
        uint8_t frame_a, slot_a, frame_b, slot_b;
        dji_ctrl_slot(a, &frame_a, &slot_a);
        dji_ctrl_slot(b, &frame_b, &slot_b);
        return dji_feedback_id(a) == dji_feedback_id(b) || (frame_a == frame_b && slot_a == slot_b);
    }
    uint32_t ia[2], ib[2];
    bus_ids(a, ia);
    bus_ids(b, ib);
    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            if (ia[i] == ib[j])
            {
                return true;
            }
        }
    }
    return false;
}

bool motor_init(Motor *m, const MotorConfig *cfg, MotorGroup *group, const Motor **conflict)
{
    *conflict = NULL;
    if (is_dm(cfg) ? !dm_config_valid(cfg) : !dji_config_valid(cfg))
    {
        return false;
    }
    for (const Motor *other = group->head; other != NULL; other = other->next)
    {
        if (conflicts(cfg, other->cfg))
        {
            *conflict = other;
            return false;
        }
    }

    *m = (Motor){ .cfg = cfg };
    watchdog_register(&m->wd, cfg->name, MOTOR_OFFLINE_TIMEOUT_MS);
    m->next = group->head;
    group->head = m;
    return true;
}

MotorCaps motor_caps(const Motor *m)
{
    return is_dm(m->cfg) ? dm_caps() : dji_caps(m->cfg->type);
}

bool motor_supports_torque(const Motor *m)
{
    return motor_caps(m).torque_command;
}

bool motor_read_feedback(const Motor *m, MotorFeedback *out)
{
    rm_critical_enter();
    *out = m->fb;
    rm_critical_exit();
    out->online = watchdog_is_online(&m->wd);
    return out->online;
}

void motor_set_torque(Motor *m, float torque_nm)
{
    m->torque_cmd_nm = torque_nm;
    m->torque_set = true;
}

void motor_apply_safe_action(Motor *m, SafeAction action)
{
    m->safe_action = action;
    m->safe_set = true;
}

void motor_request_enable(Motor *m)
{
    if (is_dm(m->cfg))
    {
        m->brand.dm.want_enabled = true;
        m->brand.dm.clear_requested = true;
    }
}

void motor_request_disable(Motor *m)
{
    if (is_dm(m->cfg))
    {
        m->brand.dm.want_enabled = false;
    }
}
