/**
 * @file    fake_can.h
 * @brief   电脑测试用的假 CAN：记录发出的帧（收帧由测试直接调用 motor_receive()）
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "05_platform/can.h"

/** 清空已发帧和设置 */
void fake_can_reset(void);

/** 发出的帧数与第 i 帧 */
uint32_t fake_can_sent_count(void);
const CanFrame *fake_can_sent(uint32_t i);
CanBusId fake_can_sent_bus(uint32_t i);

/** 设定某路总线是否为 FD（默认都是经典） */
void fake_can_set_bus_fd(CanBusId bus, bool fd);

/** 之后的 can_send 全部返回 false（模拟发送队列满） */
void fake_can_set_send_fail(bool fail);
