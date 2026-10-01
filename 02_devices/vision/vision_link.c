/**
 * @file    vision_link.c
 * @brief   与上位机的通信链路，见 vision_link.h
 */
#include "vision_link.h"

#include <string.h>

void vision_link_init(VisionLink *self)
{
    *self = (VisionLink){ 0 };
    watchdog_register(&self->wd, "vision_link", VISION_LINK_TIMEOUT_MS);
}

/*
 * 收到的字节先攒进 buf，再从 buf 开头反复找帧：合法帧取走并喂狗，坏字节丢一个重新找，不够一帧就等下一段。
 * 帧内容暂不处理（消息字段等视觉组协议，ADR 0037），只统计帧数和最后的 ID。
 */
void vision_link_on_bytes(VisionLink *self, const uint8_t *data, uint32_t len)
{
    uint32_t in = 0u;
    for (;;)
    {
        /* 把能放下的新字节放进缓冲区 */
        while (in < len && self->len < VISION_LINK_BUF_LEN)
        {
            self->buf[self->len++] = data[in++];
        }
        if (self->len == 0u)
        {
            return;
        }

        VisionFrame f;
        size_t consumed;
        switch (vision_frame_check(self->buf, self->len, &f))
        {
            case VISION_FRAME_NEED_MORE:
                /* 缓冲区能放下最长的帧，所以缓冲区满时不会走到这里；这里只可能是字节还没到齐 */
                return;
            case VISION_FRAME_BAD:
                consumed = 1u; /* 丢掉开头一个字节，重新找帧头 */
                self->bad_bytes++;
                break;
            case VISION_FRAME_OK:
            default:
                consumed = f.frame_len;
                self->frames++;
                self->last_id = f.id;
                watchdog_feed(&self->wd);
                break;
        }
        memmove(self->buf, self->buf + consumed, self->len - consumed); /* 去掉已处理的字节 */
        self->len -= (uint32_t)consumed;
    }
}
