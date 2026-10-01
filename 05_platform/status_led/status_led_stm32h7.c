/**
 * @file    status_led.c
 * @brief   DM-MC02 状态灯：PA7（SPI6_MOSI）上的一颗 WS2812
 * @note    依赖 CubeMX 中 SPI6 的三项设置，任一项不对灯就会乱色或不亮，而且没有任何报错：
 *          内核时钟 HSE 24 MHz、分频 4（= 6 MHz）、Data Size 8 bit（REGEN_CHECKLIST 核对）。
 */
#include "05_platform/status_led/status_led.h"

#include "ws2812.h"

#include <spi.h>

/* 124 字节在 6 MHz 下约 165 µs；超时只用来防止外设卡死，不用来控制节奏 */
#define SEND_TIMEOUT_MS 2u

/* 颜色 24 字节 + 复位 100 字节，一次发完，复位段在两者之间不会被别的操作插进来。
 * 阻塞发送由 CPU 逐字节写 SPI，缓冲区放在默认内存（DTCM）即可。 */
static uint8_t frame[WS2812_BYTES_PER_LED + WS2812_LATCH_BYTES];

void rm_status_led_set(uint8_t red, uint8_t green, uint8_t blue)
{
    /* 复位段在 .bss 里本来就是 0，这里只改颜色部分 */
    ws2812_encode(frame, red, green, blue);
    (void)HAL_SPI_Transmit(&hspi6, frame, (uint16_t)sizeof(frame), SEND_TIMEOUT_MS);
}
