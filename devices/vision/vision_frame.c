/**
 * @file    vision_frame.c
 * @brief   0x5A 帧，见 vision_frame.h
 */
#include "vision_frame.h"

#include <string.h>

#include "core/util/crc.h"

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
