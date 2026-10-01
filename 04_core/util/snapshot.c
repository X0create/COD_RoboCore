/**
 * @file    snapshot.c
 * @brief   快照，见 snapshot.h
 */
#include "snapshot.h"

#include <string.h>

#include "04_core/os/critical.h"
#include "05_platform/time/time.h"

void snapshot_write(Snapshot *snap, void *slot, const void *src, size_t size)
{
    const uint64_t now_us = rm_time_now_us();
    rm_critical_enter();
    memcpy(slot, src, size);
    snap->stamp_us = now_us;
    snap->written = true;
    rm_critical_exit();
}

bool snapshot_read(const Snapshot *snap, const void *slot, void *out, size_t size,
                   uint32_t max_age_ms)
{
    /* 先取时刻再进临界区：临界区尽量短。这之后若恰好有新的写入，时刻会比 now_us 还新，按“刚写入”处理 */
    const uint64_t now_us = rm_time_now_us();
    bool fresh;

    rm_critical_enter();
    fresh = snap->written
            && (max_age_ms == SNAPSHOT_ANY_AGE || snap->stamp_us >= now_us
                || now_us - snap->stamp_us <= (uint64_t)max_age_ms * 1000u);
    if (fresh)
    {
        memcpy(out, slot, size);
    }
    rm_critical_exit();
    return fresh;
}
