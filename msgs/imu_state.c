/**
 * @file    imu_state.c
 * @brief   姿态消息，见 imu_state.h
 */
#include "imu_state.h"

bool imu_state_claim(ImuStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

void imu_state_publish(ImuStateTopic *topic, const ImuState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

bool imu_state_read(const ImuStateTopic *topic, ImuState *out, uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}
