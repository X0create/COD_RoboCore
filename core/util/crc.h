/**
 * @file    crc.h
 * @brief   裁判系统协议用的 CRC8 / CRC16（纯计算，电脑上可测）
 * @note    两种校验都是 RoboMaster 裁判系统串口协议规定的，图传链路、上位机通信沿用同一套：
 *          - CRC8：多项式 x^8+x^5+x^4+1（0x31，反射后 0x8C），初值 0xFF，无结果异或；
 *          - CRC16：多项式 0x1021（反射后 0x8408），初值 0xFFFF，无结果异或，即 CRC-16/MCRF4XX
 *            （"123456789" 的校验值 0x6F91）。**不是** CRC-16/MODBUS，两者初值相同但多项式不同。
 *
 *          长度参数有两种含义，不要混用：
 *          - `crc*_calc` 的 `len` 是要参与计算的字节数；
 *          - `crc*_append` / `crc*_verify` 的 `frame_len` 是**整帧**长度，包含末尾的校验字节。
 *          帧长度由调用者（协议解析）在收帧时检查一次，本模块不再检查（见 CODING_STANDARD 第 2 节）。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CRC8_INIT  0xFFu
#define CRC16_INIT 0xFFFFu

/**
 * @brief   计算一段字节的 CRC8
 * @param   init  新算一帧时传 CRC8_INIT；分段计算时传上一段的结果
 * @return  len 为 0 时原样返回 init
 */
uint8_t crc8_calc(const uint8_t *data, size_t len, uint8_t init);

/** 把前 frame_len - 1 字节的 CRC8 写进最后一个字节；frame_len 至少为 2 */
void crc8_append(uint8_t *frame, size_t frame_len);

/** 最后一个字节是否等于前 frame_len - 1 字节的 CRC8；frame_len 至少为 2 */
bool crc8_verify(const uint8_t *frame, size_t frame_len);

/** 计算一段字节的 CRC16；用法同 crc8_calc，新算一帧时传 CRC16_INIT */
uint16_t crc16_calc(const uint8_t *data, size_t len, uint16_t init);

/** 把前 frame_len - 2 字节的 CRC16 写进最后两个字节（低字节在前）；frame_len 至少为 3 */
void crc16_append(uint8_t *frame, size_t frame_len);

/** 最后两个字节（低字节在前）是否等于前 frame_len - 2 字节的 CRC16；frame_len 至少为 3 */
bool crc16_verify(const uint8_t *frame, size_t frame_len);

#ifdef __cplusplus
}
#endif
