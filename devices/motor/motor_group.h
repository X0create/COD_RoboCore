/**
 * @file    motor_group.h
 * @brief   电机组：按控制帧打包、统一发送（运行时契约第 6 节“电机组打包”）
 * @note    control 任务每个周期：
 *            1. 子系统调用 motor_set_torque() / motor_apply_safe_action()，只写槽位；
 *            2. 需要全车停时调用 motor_group_apply_stop_all()，每个电机改写成它的 stop_action；
 *            3. 周期末尾调用一次 motor_group_flush()：按帧打包入队，然后清空全部槽位。
 *          本周期没写的槽位和离线电机的槽位填零力矩，不“保持上一帧”。
 *          达妙电机每台每周期一帧：需要时是使能 / 失能 / 清错命令，否则是 MIT 帧；FD 总线上发 FD 帧。
 *          全部函数只在 control 任务里调用（motor_init 除外，它在初始化阶段）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "motor.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 每路 CAN 上 DJI 电调的控制帧：0x200（C6x0 1–4）、0x1FF（C6x0 5–8 / GM6020 1–4）、0x2FF（GM6020 5–7） */
#define DJI_CTRL_FRAMES 3u

struct MotorGroup
{
    Motor *head; /* 组内电机链表，motor_init() 加入 */
    /* 某个控制帧里的电机全部为“失能”时，发过一次 0 就不再发（DJI 的失能 = 发 0 后停止发送） */
    bool disabled_sent[CAN_BUS_COUNT][DJI_CTRL_FRAMES];
    uint32_t tx_dropped; /* can_send 失败（发送队列满）被丢弃的帧数，调试用 */
};

/** 全车停：每个电机本周期改写成它配置的 stop_action（运行时契约第 5 节） */
void motor_group_apply_stop_all(MotorGroup *group);

/** 按控制帧打包入队，然后清空全部槽位 */
void motor_group_flush(MotorGroup *group);

#ifdef __cplusplus
}
#endif
