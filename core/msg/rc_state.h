/**
 * @file    rc_state.h
 * @brief   遥控器状态消息（DR16 发布）
 * @note    只有通过校验的帧才发布，所以话题时间戳就是“最后一次收到合法帧”的时刻。
 *          遥控是否丢失以读话题时的新旧为准：rc_state_read(topic, &rc, RC_LOST_TIMEOUT_MS) 返回 false 即丢失
 *          （ADR 0030）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/msg/topic.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 超过这么久没有合法帧就算遥控丢失（ADR 0030：用户 2026-09-28 决定沿用旧工程的 200 ms） */
#define RC_LOST_TIMEOUT_MS 200u

/** 摇杆减去中位后的最大幅度（原始值 364–1684，中位 1024） */
#define RC_CH_MAX 660

/** 拨杆位置，取值与 DR16 协议一致 */
typedef enum
{
    RC_SW_UP = 1,
    RC_SW_DOWN = 2,
    RC_SW_MID = 3,
} RcSwitch;

/** 键盘位掩码 keys 的各位 */
enum
{
    RC_KEY_W = 1u << 0,
    RC_KEY_S = 1u << 1,
    RC_KEY_A = 1u << 2,
    RC_KEY_D = 1u << 3,
    RC_KEY_SHIFT = 1u << 4,
    RC_KEY_CTRL = 1u << 5,
    RC_KEY_Q = 1u << 6,
    RC_KEY_E = 1u << 7,
    RC_KEY_R = 1u << 8,
    RC_KEY_F = 1u << 9,
    RC_KEY_G = 1u << 10,
    RC_KEY_Z = 1u << 11,
    RC_KEY_X = 1u << 12,
    RC_KEY_C = 1u << 13,
    RC_KEY_V = 1u << 14,
    RC_KEY_B = 1u << 15,
};

typedef struct
{
    /* 0–3 为摇杆、4 为拨轮，已减去中位 1024。0–3 保证在 ±RC_CH_MAX 内；拨轮不做范围检查（见 dr16.c）。
     * 各通道对应哪根摇杆的哪个方向沿用旧工程的编号，待上板核对（VERIFICATION_TODO） */
    int16_t ch[5];
    RcSwitch sw[2]; /* [0] 左、[1] 右：沿用旧工程的注释，待上板核对 */
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    bool mouse_left;
    bool mouse_right;
    uint16_t keys; /* RC_KEY_* 位掩码 */
} RcState;

_Static_assert(sizeof(RcState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

typedef struct
{
    Topic base;
    RcState data;
} RcStateTopic;

RM_NODISCARD static inline bool rc_state_claim(RcStateTopic *topic, const char *owner)
{
    return topic_claim(&topic->base, owner);
}

static inline void rc_state_publish(RcStateTopic *topic, const RcState *state)
{
    topic_publish(&topic->base, &topic->data, state, sizeof(*state));
}

/** @return false：从未发布或比 max_age_ms 更旧（TOPIC_ANY_AGE 表示不限），out 不被修改 */
RM_NODISCARD static inline bool rc_state_read(const RcStateTopic *topic, RcState *out,
                                              uint32_t max_age_ms)
{
    return topic_read(&topic->base, &topic->data, out, sizeof(*out), max_age_ms);
}

#ifdef __cplusplus
}
#endif
