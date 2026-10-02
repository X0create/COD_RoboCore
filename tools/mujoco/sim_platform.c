/**
 * @file    sim_platform.c
 * @brief   实现平台同名函数，让原有电机发送出口链接到虚拟 CAN
 */
#include "sim_platform.h"

#include <string.h>

#include "02_devices/motor/dji_motor.h"
#include "02_devices/motor/motor.h"
#include "04_core/os/critical.h"
#include "05_platform/can/can.h"
#include "05_platform/time/time.h"

static uint64_t sim_now_us;
static int16_t current_raw[8];

void sim_platform_begin(uint64_t now_us)
{
    sim_now_us = now_us;
    memset(current_raw, 0, sizeof(current_raw));
}

float sim_platform_torque(uint8_t id)
{
    return (float)current_raw[id - 1u] / DJI_M3508_RAW_PER_NM;
}

bool rm_time_init(void)
{
    return true;
}

uint64_t rm_time_now_us(void)
{
    return sim_now_us;
}

/* 整个 C 控制循环只在 Python 主线程运行，没有中断和第二个 C 线程。 */
void rm_critical_enter(void)
{
}

void rm_critical_exit(void)
{
}

bool can_bus_is_fd(CanBusId bus)
{
    (void)bus;
    return false;
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    if (bus != CAN_BUS_1 || frame->len != 8u || frame->is_fd)
    {
        return false;
    }
    unsigned offset;
    if (frame->id == dji_ctrl_frame_id(0u))
    {
        offset = 0u;
    }
    else if (frame->id == dji_ctrl_frame_id(1u))
    {
        offset = 4u;
    }
    else
    {
        return false;
    }
    for (unsigned slot = 0u; slot < 4u; slot++)
    {
        current_raw[offset + slot] =
            (int16_t)(((uint16_t)frame->data[slot * 2u] << 8) | frame->data[slot * 2u + 1u]);
    }
    return true;
}
