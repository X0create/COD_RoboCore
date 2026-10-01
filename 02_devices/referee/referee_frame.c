/**
 * @file    referee_frame.c
 * @brief   0xA5 帧检查，见 referee_frame.h
 */
#include "referee_frame.h"

#include "04_core/util/crc.h"

RefereeFrameStatus referee_frame_check(const uint8_t *buf, size_t len, uint16_t max_data_len,
                                       RefereeFrame *out)
{
    if (len == 0u)
    {
        return REFEREE_FRAME_NEED_MORE;
    }
    if (buf[0] != REFEREE_FRAME_SOF)
    {
        return REFEREE_FRAME_BAD;
    }
    if (len < REFEREE_FRAME_HEADER_LEN)
    {
        return REFEREE_FRAME_NEED_MORE;
    }
    if (!crc8_verify(buf, REFEREE_FRAME_HEADER_LEN))
    {
        return REFEREE_FRAME_BAD;
    }
    const uint16_t data_len = (uint16_t)(buf[1] | (buf[2] << 8));
    if (data_len > max_data_len)
    {
        return REFEREE_FRAME_BAD;
    }
    const size_t frame_len = (size_t)data_len + REFEREE_FRAME_OVERHEAD;
    if (len < frame_len)
    {
        return REFEREE_FRAME_NEED_MORE;
    }
    if (!crc16_verify(buf, frame_len))
    {
        return REFEREE_FRAME_BAD;
    }
    out->cmd_id = (uint16_t)(buf[5] | (buf[6] << 8));
    out->data = &buf[REFEREE_FRAME_HEADER_LEN + REFEREE_FRAME_CMD_LEN];
    out->data_len = data_len;
    out->frame_len = frame_len;
    return REFEREE_FRAME_OK;
}
