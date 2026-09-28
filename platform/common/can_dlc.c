/**
 * @file    can_dlc.c
 * @brief   CAN 数据字节数与 DLC 编码换算，见 can_dlc.h
 */
#include "can_dlc.h"

/* DLC 编码 0–15 对应的字节数 */
static const uint8_t dlc_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

uint8_t can_dlc_to_len(uint8_t dlc)
{
    return dlc_len[(dlc > 15u) ? 15u : dlc];
}

bool can_len_to_dlc(uint8_t len, uint8_t *dlc)
{
    for (uint8_t code = 0u; code < 16u; code++)
    {
        if (dlc_len[code] == len)
        {
            *dlc = code;
            return true;
        }
    }
    return false;
}
