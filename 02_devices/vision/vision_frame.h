/**
 * @file    vision_frame.h
 * @brief   与上位机通信的帧格式（0x5A 帧）：检查与组帧（纯计算，电脑上可测）
 * @note    帧 = SOF 0x5A、数据长度 1 字节、ID 1 字节、帧头 CRC8（前 3 字节）+ 数据 + 整帧 CRC16 2 字节小端
 *          （附录 A.6，参考 standard_robot_pp_ros2）。CRC8 / CRC16 用与裁判系统相同的算法（04_core/util/crc）。
 *          具体消息 ID 和字段等视觉组确定后再定（ADR 0037）；帧格式本身也要与视觉组核对。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VISION_FRAME_SOF        0x5Au
#define VISION_FRAME_HEADER_LEN 4u
#define VISION_FRAME_TAIL_LEN   2u
#define VISION_FRAME_OVERHEAD   (VISION_FRAME_HEADER_LEN + VISION_FRAME_TAIL_LEN)
#define VISION_FRAME_MAX_DATA   255u

typedef enum
{
    VISION_FRAME_OK,
    VISION_FRAME_NEED_MORE,
    VISION_FRAME_BAD,
} VisionFrameStatus;

typedef struct
{
    uint8_t id;
    const uint8_t *data; /* 指向缓冲区里的数据段 */
    uint8_t data_len;
    size_t frame_len; /* 整帧字节数 */
} VisionFrame;

/** 检查 buf 开头是否为一帧（用法同 ref_frame_check：OK 取走、BAD 丢一字节、NEED_MORE 等） */
VisionFrameStatus vision_frame_check(const uint8_t *buf, size_t len, VisionFrame *out);

/**
 * @brief   组一帧
 * @param   out  至少 data_len + VISION_FRAME_OVERHEAD 字节
 * @return  整帧字节数
 */
size_t vision_frame_encode(uint8_t id, const uint8_t *data, uint8_t data_len, uint8_t *out);

#ifdef __cplusplus
}
#endif
