/**
 * @file    vision.h
 * @brief   视觉通信：0x5A 帧层 + USB CDC 通道（ADR 0037）
 * @note    按节排列：帧层 vision_frame、通道 vision_link。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "04_core/watchdog/watchdog.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ================================================================== */
/* 帧层 vision_frame
 *
 * 与上位机通信的帧格式（0x5A 帧）：检查与组帧（纯计算，电脑上可测）
 * 帧 = SOF 0x5A、数据长度 1 字节、ID 1 字节、帧头 CRC8（前 3 字节）+ 数据 + 整帧 CRC16 2 字节小端
 * （附录 A.6，参考 standard_robot_pp_ros2）。CRC8 / CRC16 用与裁判系统相同的算法（04_core/util/crc）。
 * 具体消息 ID 和字段等视觉组确定后再定（ADR 0037）；帧格式本身也要与视觉组核对。
 */

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

/** 检查 buf 开头是否为一帧（用法同 referee_frame_check：OK 取走、BAD 丢一字节、NEED_MORE 等） */
VisionFrameStatus vision_frame_check(const uint8_t *buf, size_t len, VisionFrame *out);

/**
 * @brief   组一帧
 * @param   out  至少 data_len + VISION_FRAME_OVERHEAD 字节
 * @return  整帧字节数
 */
size_t vision_frame_encode(uint8_t id, const uint8_t *data, uint8_t data_len, uint8_t *out);

/* ================================================================== */
/* 通道 vision_link
 *
 * 与上位机（视觉）的通信链路：从字节流里找出 0x5A 帧
 * COD-H7-Template 的 MiniPC.c 是空壳（发送只发 1 字节、接收不处理），没有协议可移植。
 * 本版只做通道和帧层（ADR 0037）：找帧、校验、计数；消息 ID 和字段、下行姿态与时间戳对齐
 * （运行时契约第 2 节）等视觉组确定协议后再加，届时在 on_frame 里按 ID 解析，保存 VisionCmd 并提供读取函数（同 dr16_read）。
 */

#define VISION_LINK_BUF_LEN    (VISION_FRAME_MAX_DATA + VISION_FRAME_OVERHEAD)
#define VISION_LINK_TIMEOUT_MS 200u /* 只用于上线 / 离线日志 */

typedef struct
{
    Watchdog wd;
    uint8_t buf[VISION_LINK_BUF_LEN];
    uint32_t len;
    uint32_t frames;    /* 校验通过的帧数（调试用） */
    uint8_t last_id;    /* 最近一帧的 ID（调试用） */
    uint32_t bad_bytes; /* 找帧时丢弃的字节数（调试用） */
} VisionLink;

/** @pre 初始化阶段调用 */
void vision_link_init(VisionLink *self);

/** 喂入从 USB 读到的一段字节（comm_rx_task 里调用） */
void vision_link_on_bytes(VisionLink *self, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif
