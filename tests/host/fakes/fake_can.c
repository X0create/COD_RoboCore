/**
 * @file    fake_can.c
 * @brief   假 CAN，见 fake_can.h；代替 platform/stm32h7/can.c
 */
#include "fake_can.h"

#include <stddef.h>

#define MAX_SUBS 32u
#define MAX_SENT 64u

typedef struct
{
    CanBusId bus;
    uint32_t first_id;
    uint32_t last_id;
    CanRxHandler handler;
    void *ctx;
} Sub;

static Sub subs[MAX_SUBS];
static uint32_t sub_count;
static CanFrame sent[MAX_SENT];
static CanBusId sent_bus[MAX_SENT];
static uint32_t sent_count;
static bool send_fail;

void fake_can_reset(void)
{
    sub_count = 0u;
    sent_count = 0u;
    send_fail = false;
}

bool can_subscribe_range(CanBusId bus, uint32_t first_id, uint32_t last_id, CanRxHandler handler,
                         void *ctx)
{
    if (sub_count >= MAX_SUBS)
    {
        return false;
    }
    for (uint32_t i = 0u; i < sub_count; i++)
    {
        if (subs[i].bus == bus && first_id <= subs[i].last_id && subs[i].first_id <= last_id)
        {
            return false; /* 与真实实现一样拒绝重叠 */
        }
    }
    subs[sub_count++] = (Sub){ bus, first_id, last_id, handler, ctx };
    return true;
}

bool can_subscribe(CanBusId bus, uint32_t id, CanRxHandler handler, void *ctx)
{
    return can_subscribe_range(bus, id, id, handler, ctx);
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    if (send_fail || sent_count >= MAX_SENT)
    {
        return false;
    }
    sent_bus[sent_count] = bus;
    sent[sent_count++] = *frame;
    return true;
}

bool fake_can_deliver(CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    for (uint32_t i = 0u; i < sub_count; i++)
    {
        if (subs[i].bus == bus && id >= subs[i].first_id && id <= subs[i].last_id)
        {
            CanFrame frame = { .id = id, .len = len };
            for (uint8_t k = 0u; k < len; k++)
            {
                frame.data[k] = data[k];
            }
            subs[i].handler(&frame, subs[i].ctx);
            return true;
        }
    }
    return false;
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
