/**
 * @file    fake_can.h
 * @brief   电脑测试用的假 CAN：记录订阅和发出的帧，测试可以把反馈帧“送达”订阅者
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/can.h"

/** 清空订阅和已发帧 */
void fake_can_reset(void);

/** 按订阅把一帧交给回调（相当于硬件收到 + comm_rx 分发）；没有订阅者时返回 false */
bool fake_can_deliver(CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len);

/** 发出的帧数与第 i 帧 */
uint32_t fake_can_sent_count(void);
const CanFrame *fake_can_sent(uint32_t i);
CanBusId fake_can_sent_bus(uint32_t i);

/** 之后的 can_send 全部返回 false（模拟发送队列满） */
void fake_can_set_send_fail(bool fail);
