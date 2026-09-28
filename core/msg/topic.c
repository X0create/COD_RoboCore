/**
 * @file    topic.c
 * @brief   话题的通用实现，见 topic.h
 */
#include "topic.h"

#include <string.h>

#include "core/os/critical.h"
#include "platform/time.h"

bool topic_claim(Topic *topic, const char *owner)
{
    if (topic->owner != NULL)
    {
        return false;
    }
    topic->owner = owner;
    return true;
}

void topic_publish(Topic *topic, void *slot, const void *src, size_t size)
{
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    memcpy(slot, src, size);
    topic->stamp_us = now_us;
    topic->published = true;
    rm_critical_exit();
}

bool topic_read(const Topic *topic, const void *slot, void *out, size_t size, uint32_t max_age_ms)
{
    /* 先取时刻再进临界区：临界区尽量短。这之后若恰好有新的发布，时间戳会比 now_us 还新，按“刚发布”处理 */
    const uint64_t now_us = rm_time_now_us();
    bool fresh;

    rm_critical_enter();
    fresh = topic->published
            && (max_age_ms == TOPIC_ANY_AGE || topic->stamp_us >= now_us
                || now_us - topic->stamp_us <= (uint64_t)max_age_ms * 1000u);
    if (fresh)
    {
        memcpy(out, slot, size);
    }
    rm_critical_exit();
    return fresh;
}
