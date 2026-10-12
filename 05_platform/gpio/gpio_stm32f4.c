/**
 * @file    gpio_stm32f4.c
 * @brief   STM32F4 的 GPIO 输入（见 05_platform/gpio/gpio.h）
 * @note    C 板上还没有接零点开关，表里全部为空，gpio_read() 一律返回低电平；
 *          接了开关时在表里填上引脚，并在 CubeMX 里把这些引脚配成 GPIO_Input。
 */
#include "05_platform/gpio/gpio.h"

#include <stddef.h>

#include <stm32f4xx.h>

typedef struct
{
    GPIO_TypeDef *port;
    uint32_t pin_mask;
} PinMap;

static const PinMap pins[GPIO_IN_COUNT] = { { 0 } }; /* 全部未接 */

bool gpio_read(GpioInput in)
{
    const PinMap *p = &pins[in];
    return p->port != NULL && (p->port->IDR & p->pin_mask) != 0u;
}
