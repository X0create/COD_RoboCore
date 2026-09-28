/**
 * @file    can_dlc.h
 * @brief   CAN 数据字节数与 DLC 编码（0–15）互相换算（纯计算，电脑上可测）
 * @note    DLC 0–8 就是字节数；9–15 依次表示 12、16、20、24、32、48、64 字节（只有 CAN FD 帧用得到）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** DLC 编码 → 字节数；超过 15 的编码按 15 处理 */
uint8_t can_dlc_to_len(uint8_t dlc);

/**
 * @brief   字节数 → DLC 编码
 * @param   dlc  输出的编码
 * @return  false：这个字节数不是合法的 CAN 帧长度（例如 FD 帧 10 字节、任何帧超过 64 字节）
 */
bool can_len_to_dlc(uint8_t len, uint8_t *dlc);

#ifdef __cplusplus
}
#endif
