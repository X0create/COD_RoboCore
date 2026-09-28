/**
 * @file    fake_time.h
 * @brief   电脑测试用的假时钟：rm_time_now_us() 返回测试设定的时刻
 */
#pragma once

#include <stdint.h>

void fake_time_set_us(uint64_t now_us);
void fake_time_advance_ms(uint32_t ms);
