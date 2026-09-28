/**
 * @file    vt_rc_state.c
 * @brief   VT13 遥控器状态消息，见 vt_rc_state.h
 */
#include "vt_rc_state.h"

bool vt_rc_state_claim(VtRcStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

void vt_rc_state_publish(VtRcStateTopic *topic, const VtRcState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

bool vt_rc_state_read(const VtRcStateTopic *topic, VtRcState *out, uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}
