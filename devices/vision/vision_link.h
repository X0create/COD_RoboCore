/**
 * @file    vision_link.h
 * @brief   与上位机（视觉）的通信链路：从字节流里找出 0x5A 帧
 * @note    COD-H7-Template 的 MiniPC.c 是空壳（发送只发 1 字节、接收不处理），没有协议可移植。
 *          本版只做通道和帧层（ADR 0037）：找帧、校验、计数；消息 ID 和字段、下行姿态与时间戳对齐
 *          （运行时契约第 2 节）等视觉组确定协议后再加，届时在 on_frame 里按 ID 解析并发布 VisionCmd。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/watchdog/watchdog.h"
#include "devices/vision/vision_frame.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

/** 喂入从 USB 读到的一段字节（comm_rx 任务里调用） */
void vision_link_on_bytes(VisionLink *self, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif
