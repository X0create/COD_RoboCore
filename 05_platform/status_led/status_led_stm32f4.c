/**
 * @file    status_led_stm32f4.c
 * @brief   C 板状态灯：板载 RGB 灯，三种颜色各接一路 TIM5 PWM，占空比就是亮度
 * @note    CubeMX 配置：TIM5 CH1 = PH10 蓝、CH2 = PH11 绿、CH3 = PH12 红，周期 255（ARR = 255），
 *          所以 0–255 的亮度直接写进比较寄存器即可。
 *          和 MC02 的 WS2812 不同，这里不需要编码，第一次调用时打开三路 PWM。
 */
#include "05_platform/status_led/status_led.h"

#include <stdbool.h>

#include <tim.h>

static bool started;

void status_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!started)
    {
        /* 失败时灯不亮，状态灯不影响控制，不处理 */
        (void)HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_1);
        (void)HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_2);
        (void)HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_3);
        started = true;
    }
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_1, blue);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_2, green);
    __HAL_TIM_SET_COMPARE(&htim5, TIM_CHANNEL_3, red);
}
