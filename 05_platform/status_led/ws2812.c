/**
 * @file    ws2812.c
 * @brief   WS2812 的 SPI 编码，见 ws2812.h
 * @note    编码方式参考 COD_UniCFramework `dev_ws2812.c`
 */
#include "ws2812.h"

void ws2812_encode(uint8_t out[WS2812_BYTES_PER_LED], uint8_t red, uint8_t green, uint8_t blue)
{
    /* WS2812 的发送顺序是 绿、红、蓝，每个颜色从最高位开始；每一位编码成一个 SPI 字节（0 码 / 1 码的高电平宽度不同） */
    const uint8_t channels[3] = { green, red, blue };
    uint32_t pos = 0u;
    for (uint32_t ch = 0u; ch < 3u; ch++)
    {
        for (int bit = 7; bit >= 0; bit--)
        {
            const uint8_t is_one = (uint8_t)((channels[ch] >> bit) & 1u);
            out[pos] = is_one ? WS2812_CODE_1 : WS2812_CODE_0;
            pos++;
        }
    }
}
