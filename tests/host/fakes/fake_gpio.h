/**
 * @file    fake_gpio.h
 * @brief   gpio 的假实现：测试直接设定每个输入引脚的电平
 */
#pragma once

#include <stdbool.h>

#include "05_platform/gpio/gpio.h"

/** 全部引脚恢复低电平 */
void fake_gpio_reset(void);

void fake_gpio_set(GpioInput in, bool high);
