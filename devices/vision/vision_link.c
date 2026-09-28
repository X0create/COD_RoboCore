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

void vision_link_on_bytes(VisionLink *self, const uint8_t *data, uint32_t len)
{
    uint32_t in = 0u;
    for (;;)
    {
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
        memmove(self->buf, self->buf + consumed, self->len - consumed);
        self->len -= (uint32_t)consumed;
    }
}
