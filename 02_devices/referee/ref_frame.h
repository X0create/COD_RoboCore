/**
 * @file    ref_frame.h
 * @brief   裁判系统帧格式（0xA5 帧）的检查：裁判系统串口和图传链路共用（纯计算，电脑上可测）
 * @note    帧 = 帧头 5 字节（SOF 0xA5、数据长度 2 字节小端、包序号、帧头 CRC8）+ 命令码 2 字节小端
 *          + 数据 + 整帧 CRC16 2 字节（附录 A.5；帧格式与赛季版本无关，各命令的数据长度随赛季变化）。
 *          用法：接收方把字节攒进缓冲区，每次从缓冲区开头调用 ref_frame_check()：
 *          OK 就取走一帧，BAD 就丢掉开头一个字节重新找帧头，NEED_MORE 就等更多字节。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define REF_FRAME_SOF        0xA5u
#define REF_FRAME_HEADER_LEN 5u
#define REF_FRAME_CMD_LEN    2u
#define REF_FRAME_TAIL_LEN   2u
#define REF_FRAME_OVERHEAD   (REF_FRAME_HEADER_LEN + REF_FRAME_CMD_LEN + REF_FRAME_TAIL_LEN)

typedef enum
{
    REF_FRAME_OK,        /* 开头是一帧完整、校验通过的帧 */
    REF_FRAME_NEED_MORE, /* 目前为止没发现错误，但字节还不够 */
    REF_FRAME_BAD, /* 开头不是合法帧（帧头不对、CRC 错、长度超过 max_data_len） */
} RefFrameStatus;

typedef struct
{
    uint16_t cmd_id;
    const uint8_t *data; /* 指向缓冲区里的数据段 */
    uint16_t data_len;
    size_t frame_len; /* 整帧字节数，取走时用 */
} RefFrame;

/**
 * @brief   检查 buf 开头是否为一帧
 * @param   max_data_len  允许的最大数据长度（接收缓冲区的大小决定），更长的当作坏帧
 */
RefFrameStatus ref_frame_check(const uint8_t *buf, size_t len, uint16_t max_data_len,
                               RefFrame *out);

#ifdef __cplusplus
}
#endif
