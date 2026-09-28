/**
 * @file    spi.h
 * @brief   SPI：按用途命名的设备，阻塞传输（ADR 0033）
 * @note    - 设备按用途命名（SPI_DEV_IMU_ACCEL…），平台实现里用一张表对应到总线和片选脚，设备驱动里没有引脚信息；
 *          - 拉片选和占用总线是同一个操作：spi_select() 取得总线后才拉低片选，spi_deselect() 释放。
 *            一个事务（写地址 + 读数据）必须在一次 select / deselect 之间完成，否则共用总线的另一个设备
 *            可能插进来，两个片选同时为低、两颗芯片同时驱动 MISO，数据错且不报错（运行时契约第 1 节）；
 *          - 阻塞传输：BMI088 一次 17 字节约 18 µs，不值得用 DMA（《架构设计》任务划分）。
 *          只能在任务里调用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    SPI_DEV_IMU_ACCEL, /* BMI088 加速度计 */
    SPI_DEV_IMU_GYRO,  /* BMI088 陀螺仪（与加速度计共用一条总线） */
    SPI_DEV_COUNT,
} SpiDevice;

/** 单次传输的超时：总线出故障时最多阻塞这么久，不能用 HAL 例程常见的 1000 ms */
#define SPI_TIMEOUT_MS 10u

/**
 * @brief   取得这个设备所在的总线，并拉低它的片选
 * @return  false：总线正被别的设备占用（调用顺序有误），片选没有动
 */
RM_NODISCARD bool spi_select(SpiDevice dev);

/** 拉高片选，释放总线 @pre 已 spi_select(dev) 成功 */
void spi_deselect(SpiDevice dev);

/**
 * @brief   同时发送 tx、接收 rx，各 len 字节
 * @return  false：超时或硬件错误
 * @pre     已 spi_select(dev) 成功
 */
RM_NODISCARD bool spi_transfer(SpiDevice dev, const uint8_t *tx, uint8_t *rx, uint16_t len);

#ifdef __cplusplus
}
#endif
