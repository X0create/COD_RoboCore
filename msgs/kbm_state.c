/**
 * @file    kbm_state.c
 * @brief   图传链路键鼠消息，见 kbm_state.h
 */
#include "kbm_state.h"

bool kbm_state_claim(KbmStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

void kbm_state_publish(KbmStateTopic *topic, const KbmState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

bool kbm_state_read(const KbmStateTopic *topic, KbmState *out, uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}
