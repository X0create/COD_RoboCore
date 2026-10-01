/**
 * @file    vt_rc_state.h
 * @brief   VT13 图传遥控器状态消息（vt_link 发布）
 * @note    只有 CRC 和取值范围都通过的帧才发布；遥控是否在线以读话题时的新旧为准。
 *          目前没有使用者：操作输入标准化（command / OperatorInput）时再决定它怎么参与解锁和急停（ADR 0036）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/msg/topic.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 挡位开关（取值同 VT13 协议） */
typedef enum
{
    VT_MODE_C = 0,
    VT_MODE_N = 1,
    VT_MODE_S = 2,
} VtMode;

typedef struct
{
    int16_t ch[4]; /* 摇杆，已减中位 1024，约 ±660；各通道对应哪根摇杆待上板核对 */
    int16_t wheel;     /* 拨轮，已减中位 */
    VtMode mode;       /* 挡位开关 */
    bool pause;        /* 暂停（急停）键 */
    bool custom_left;  /* 自定义键：左 */
    bool custom_right; /* 自定义键：右 */
    bool trigger;      /* 扳机键 */
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    uint8_t mouse_left; /* 鼠标左、右、中键，各 2 位原始值 */
    uint8_t mouse_right;
    uint8_t mouse_middle;
    uint16_t keys; /* 位定义同 RcState 的 RC_KEY_* */
} VtRcState;

_Static_assert(sizeof(VtRcState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

typedef struct
{
    Topic base;
    VtRcState data;
} VtRcStateTopic;

RM_NODISCARD static inline bool vt_rc_state_claim(VtRcStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

static inline void vt_rc_state_publish(VtRcStateTopic *topic, const VtRcState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

RM_NODISCARD static inline bool vt_rc_state_read(const VtRcStateTopic *topic, VtRcState *out,
                                                 uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}

#ifdef __cplusplus
}
#endif
