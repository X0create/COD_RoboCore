/**
 * @file    fake_time.c
 * @brief   假时钟，见 fake_time.h；代替 05_platform/time/time.h 的芯片实现
 */
#include "fake_time.h"

#include "05_platform/time/time.h"

static uint64_t fake_now_us;

void fake_time_set_us(uint64_t now_us)
{
    fake_now_us = now_us;
}

void fake_time_advance_ms(uint32_t ms)
{
    fake_now_us += (uint64_t)ms * 1000u;
}

bool rm_time_init(void)
{
    return true;
}

uint64_t rm_time_now_us(void)
{
    return fake_now_us;
}
