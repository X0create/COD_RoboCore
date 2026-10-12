/**
 * @file    vision.c
 * @brief   视觉通信：0x5A 帧层 + USB CDC 通道（说明见 vision.h）
 */
#include "02_devices/vision/vision.h"

#include <string.h>

#include "04_core/util/crc.h"
#include "05_platform/time/time.h"

/* ================================================================== */
/* 帧层 vision_frame（说明见 vision.h 同名一节） */
/* ================================================================== */

/*
 * 帧格式：SOF(0x5A) | 数据长度(1) | ID(1) | CRC8(1) | 数据(n) | CRC16(2)
 * 检查顺序和返回值的含义与 referee_frame_check() 相同
 */
VisionFrameStatus vision_frame_check(const uint8_t *buf, size_t len, VisionFrame *out)
{
    if (len == 0u)
    {
        return VISION_FRAME_NEED_MORE;
    }
    if (buf[0] != VISION_FRAME_SOF)
    {
        return VISION_FRAME_BAD;
    }
    if (len < VISION_FRAME_HEADER_LEN)
    {
        return VISION_FRAME_NEED_MORE;
    }
    if (!crc8_verify(buf, VISION_FRAME_HEADER_LEN))
    {
        return VISION_FRAME_BAD;
    }
    const size_t frame_len = (size_t)buf[1] + VISION_FRAME_OVERHEAD;
    if (len < frame_len)
    {
        return VISION_FRAME_NEED_MORE;
    }
    if (!crc16_verify(buf, frame_len))
    {
        return VISION_FRAME_BAD;
    }
    out->id = buf[2];
    out->data = &buf[VISION_FRAME_HEADER_LEN];
    out->data_len = buf[1];
    out->frame_len = frame_len;
    return VISION_FRAME_OK;
}

/* 组一帧：填帧头 → 算 CRC8 → 拷数据 → 算 CRC16；out 至少 data_len + VISION_FRAME_OVERHEAD 字节 */
size_t vision_frame_encode(uint8_t id, const uint8_t *data, uint8_t data_len, uint8_t *out)
{
    const size_t frame_len = (size_t)data_len + VISION_FRAME_OVERHEAD;
    out[0] = VISION_FRAME_SOF;
    out[1] = data_len;
    out[2] = id;
    crc8_append(out, VISION_FRAME_HEADER_LEN);
    memcpy(&out[VISION_FRAME_HEADER_LEN], data, data_len);
    crc16_append(out, frame_len);
    return frame_len;
}

/* ================================================================== */
/* 通道 vision_link（说明见 vision.h 同名一节） */
/* ================================================================== */

void vision_link_init(VisionLink *self)
{
    *self = (VisionLink){ 0 };
    watchdog_register(&self->wd, "vision_link", VISION_LINK_TIMEOUT_MS);
}

/*
 * 收到的字节先攒进 buf，再从 buf 开头反复找帧：合法帧取走并喂狗，坏字节丢一个重新找，不够一帧就等下一段。
 * 帧内容暂不处理（消息字段等视觉组协议），只统计帧数和最后的 ID。
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
                watchdog_feed(&self->wd, rm_time_now_us());
                break;
        }
        memmove(self->buf, self->buf + consumed, self->len - consumed); /* 去掉已处理的字节 */
        self->len -= (uint32_t)consumed;
    }
}
