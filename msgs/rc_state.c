/**
 * @file    rc_state.c
 * @brief   遥控器状态消息，见 rc_state.h
 */
#include "rc_state.h"

bool rc_state_claim(RcStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

void rc_state_publish(RcStateTopic *topic, const RcState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

bool rc_state_read(const RcStateTopic *topic, RcState *out, uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}
