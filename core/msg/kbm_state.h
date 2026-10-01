/**
 * @file    kbm_state.h
 * @brief   图传链路键鼠消息（命令码 0x0304，vt_link 发布）
 * @note    操作手电脑上的键鼠经图传链路发来。目前没有使用者，操作输入标准化时接入（ADR 0036）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/msg/topic.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    bool mouse_left;
    bool mouse_right;
    uint16_t keys; /* 位定义同 RcState 的 RC_KEY_* */
} KbmState;

_Static_assert(sizeof(KbmState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

typedef struct
{
    Topic base;
    KbmState data;
} KbmStateTopic;

RM_NODISCARD static inline bool kbm_state_claim(KbmStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

static inline void kbm_state_publish(KbmStateTopic *topic, const KbmState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

RM_NODISCARD static inline bool kbm_state_read(const KbmStateTopic *topic, KbmState *out,
                                               uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}

#ifdef __cplusplus
}
#endif
