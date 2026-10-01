/**
 * @file    fake_can.c
 * @brief   假 CAN，见 fake_can.h；代替 05_platform/can/can_stm32h7.c
 */
#include "fake_can.h"

#include <stddef.h>

#define MAX_SENT 64u

static CanFrame sent[MAX_SENT];
static CanBusId sent_bus[MAX_SENT];
static uint32_t sent_count;
static bool send_fail;
static bool bus_fd[CAN_BUS_COUNT];

void fake_can_reset(void)
{
    sent_count = 0u;
    send_fail = false;
    for (int i = 0; i < (int)CAN_BUS_COUNT; i++)
    {
        bus_fd[i] = false;
    }
}

void fake_can_set_bus_fd(CanBusId bus, bool fd)
{
    bus_fd[bus] = fd;
}

bool can_bus_is_fd(CanBusId bus)
{
    return bus_fd[bus];
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    if (send_fail || sent_count >= MAX_SENT || (frame->is_fd && !bus_fd[bus]))
    {
        return false; /* 与真实实现一样：FD 帧不能发往经典总线 */
    }
    sent_bus[sent_count] = bus;
    sent[sent_count++] = *frame;
    return true;
}

uint32_t fake_can_sent_count(void)
{
    return sent_count;
}

const CanFrame *fake_can_sent(uint32_t i)
{
    return &sent[i];
}

CanBusId fake_can_sent_bus(uint32_t i)
{
    return sent_bus[i];
}

void fake_can_set_send_fail(bool fail)
{
    send_fail = fail;
}
