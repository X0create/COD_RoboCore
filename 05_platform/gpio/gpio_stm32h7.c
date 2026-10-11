/**
 * @file    gpio_stm32h7.c
 * @brief   STM32H7 的 GPIO 输入（见 05_platform/gpio/gpio.h）
 * @note    用途到引脚的对应关系照 COD 26 赛季哨兵（rmcod2026_-sentry 底盘板，同为 DM-MC02）；
 *          以后有第二块 H7 板时移到 06_boards/<板子>/board.c。
 */
#include "05_platform/gpio/gpio.h"

#include <stm32h7xx.h>

typedef struct
{
    GPIO_TypeDef *port;
    uint32_t pin_mask;
} PinMap;

static const PinMap pins[GPIO_IN_COUNT] = {
    [GPIO_IN_ZERO_SWITCH_1] = { GPIOE, 1u << 13 }, /* PE13 */
    [GPIO_IN_ZERO_SWITCH_2] = { GPIOE, 1u << 9 },  /* PE9 */
    [GPIO_IN_ZERO_SWITCH_3] = { GPIOA, 1u << 2 },  /* PA2 */
    [GPIO_IN_ZERO_SWITCH_4] = { GPIOA, 1u << 0 },  /* PA0 */
};

bool gpio_read(GpioInput in)
{
    return (pins[in].port->IDR & pins[in].pin_mask) != 0u;
}
