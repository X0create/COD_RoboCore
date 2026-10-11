/**
 * @file    fake_gpio.c
 * @brief   gpio 的假实现，见 fake_gpio.h
 */
#include "tests/host/fakes/fake_gpio.h"

static bool level[GPIO_IN_COUNT];

void fake_gpio_reset(void)
{
    for (int i = 0; i < (int)GPIO_IN_COUNT; i++)
    {
        level[i] = false;
    }
}

void fake_gpio_set(GpioInput in, bool high)
{
    level[in] = high;
}

bool gpio_read(GpioInput in)
{
    return level[in];
}
