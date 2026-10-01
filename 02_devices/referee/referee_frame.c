/**
 * @file    referee_frame.c
 * @brief   0xA5 帧检查，见 referee_frame.h
 */
#include "referee_frame.h"

#include "04_core/util/crc.h"

RefereeFrameStatus referee_frame_check(const uint8_t *buf, size_t len, uint16_t max_data_len,
                                       RefereeFrame *out)
{
    /*
     * 帧格式：SOF(0xA5) | 数据长度(2) | 序号(1) | CRC8(1) | 命令 ID(2) | 数据(n) | CRC16(2)
     * 按顺序检查，字节不够时返回 NEED_MORE 等下一段，哪一步不对就返回 BAD（调用方丢一个字节重新找帧头）
     */
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
    if (!crc8_verify(buf, REFEREE_FRAME_HEADER_LEN)) /* 先校验帧头，长度字段可信了才按它等数据 */
    {
        return REFEREE_FRAME_BAD;
    }
    const uint16_t data_len = (uint16_t)(buf[1] | (buf[2] << 8));
    if (data_len > max_data_len) /* 比缓冲区还长的帧不可能收完，按坏帧处理，避免一直等 */
    {
        return REFEREE_FRAME_BAD;
    }
    const size_t frame_len = (size_t)data_len + REFEREE_FRAME_OVERHEAD;
    if (len < frame_len)
    {
        return REFEREE_FRAME_NEED_MORE;
    }
    if (!crc16_verify(buf, frame_len)) /* 整帧校验 */
    {
        return REFEREE_FRAME_BAD;
    }
    /* 合法：data 指向缓冲区里的数据段（不拷贝），调用方在取走这帧之前用完 */
    out->cmd_id = (uint16_t)(buf[5] | (buf[6] << 8));
    out->data = &buf[REFEREE_FRAME_HEADER_LEN + REFEREE_FRAME_CMD_LEN];
    out->data_len = data_len;
    out->frame_len = frame_len;
    return REFEREE_FRAME_OK;
}
