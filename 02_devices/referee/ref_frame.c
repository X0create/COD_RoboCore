/**
 * @file    ref_frame.c
 * @brief   0xA5 帧检查，见 ref_frame.h
 */
#include "ref_frame.h"

#include "04_core/util/crc.h"

RefFrameStatus ref_frame_check(const uint8_t *buf, size_t len, uint16_t max_data_len, RefFrame *out)
{
    if (len == 0u)
    {
        return REF_FRAME_NEED_MORE;
    }
    if (buf[0] != REF_FRAME_SOF)
    {
        return REF_FRAME_BAD;
    }
    if (len < REF_FRAME_HEADER_LEN)
    {
        return REF_FRAME_NEED_MORE;
    }
    if (!crc8_verify(buf, REF_FRAME_HEADER_LEN))
    {
        return REF_FRAME_BAD;
    }
    const uint16_t data_len = (uint16_t)(buf[1] | (buf[2] << 8));
    if (data_len > max_data_len)
    {
        return REF_FRAME_BAD;
    }
    const size_t frame_len = (size_t)data_len + REF_FRAME_OVERHEAD;
    if (len < frame_len)
    {
        return REF_FRAME_NEED_MORE;
    }
    if (!crc16_verify(buf, frame_len))
    {
        return REF_FRAME_BAD;
    }
    out->cmd_id = (uint16_t)(buf[5] | (buf[6] << 8));
    out->data = &buf[REF_FRAME_HEADER_LEN + REF_FRAME_CMD_LEN];
    out->data_len = data_len;
    out->frame_len = frame_len;
    return REF_FRAME_OK;
}
