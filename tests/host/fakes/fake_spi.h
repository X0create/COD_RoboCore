/**
 * @file    fake_spi.h
 * @brief   电脑测试用的假 SPI：在两个片选后面模拟 BMI088 的加速度计和陀螺仪寄存器
 * @note    按数据手册的 SPI 时序：地址字节最高位为 1 是读，之后地址自动递增；
 *          **加速度计读时地址后有一个 dummy 字节**，陀螺仪没有。驱动若漏掉或多加 dummy，读数会错一个字节。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/spi.h"

/** 两颗芯片寄存器清零，芯片 ID 设为正确值，清除全部注入的故障 */
void fake_spi_reset(void);

void fake_spi_set_reg(SpiDevice dev, uint8_t reg, uint8_t value);
uint8_t fake_spi_get_reg(SpiDevice dev, uint8_t reg);

/** 让某个寄存器写不进去（模拟配置失败） */
void fake_spi_make_read_only(SpiDevice dev, uint8_t reg);

/** 两个片选曾同时为低（驱动违反了总线仲裁） */
bool fake_spi_both_selected_seen(void);
