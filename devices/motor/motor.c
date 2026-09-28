/**
 * @file    motor.c
 * @brief   电机统一接口，见 motor.h
 * @note    按品牌分派只在本文件和 motor_group.c 里（目前只有 DJI，达妙在第 9 步加入）。
 */
#include "motor.h"

#include <stddef.h>

#include "core/os/critical.h"
#include "dji_motor.h"
#include "motor_group.h"
#include "platform/time.h"

/* 反馈帧回调，在 comm_rx 任务里执行 */
static void on_feedback(const CanFrame *frame, void *ctx)
{
    Motor *m = ctx;
    if (frame->len != 8u)
    {
        return; /* 不是合法的反馈帧：丢弃、不喂狗 */
    }

    MotorFeedback fb;
    dji_decode_feedback(m->cfg, &m->brand.dji, frame->data, &fb);
    fb.online = false; /* 在线与否由 motor_read_feedback() 读取时计算 */
    fb.stamp_us = rm_time_now_us();

    rm_critical_enter();
    m->fb = fb;
    rm_critical_exit();
    watchdog_feed(&m->wd);
}

/* 同一路 CAN 上反馈 ID 相同，或占同一个控制帧槽位，就是冲突 */
static bool conflicts(const MotorConfig *a, const MotorConfig *b)
{
    if (a->can_bus != b->can_bus)
    {
        return false;
    }
    uint8_t frame_a, slot_a, frame_b, slot_b;
    dji_ctrl_slot(a, &frame_a, &slot_a);
    dji_ctrl_slot(b, &frame_b, &slot_b);
    return dji_feedback_id(a) == dji_feedback_id(b) || (frame_a == frame_b && slot_a == slot_b);
}

bool motor_init(Motor *m, const MotorConfig *cfg, MotorGroup *group, const Motor **conflict)
{
    *conflict = NULL;
    if (!dji_config_valid(cfg))
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
    if (!can_subscribe(cfg->can_bus, dji_feedback_id(cfg), on_feedback, m))
    {
        return false;
    }
    watchdog_register(&m->wd, cfg->name, MOTOR_OFFLINE_TIMEOUT_MS);
    m->next = group->head;
    group->head = m;
    return true;
}

MotorCaps motor_caps(const Motor *m)
{
    return dji_caps(m->cfg->type); /* 达妙加入后在这里按品牌分派 */
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
