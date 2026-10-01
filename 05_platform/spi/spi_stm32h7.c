/**
 * @file    spi_stm32h7.c
 * @brief   SPI 的 STM32H7 实现，见 05_platform/spi/spi.h
 * @note    设备表把用途对应到 CubeMX 的 SPI 句柄和片选脚（片选标签由 CubeMX 生成在 main.h，
 *          引脚为推断值，见附录 A.1）。片选脚由 CubeMX 初始化为高电平。
 */
#include "05_platform/spi/spi.h"

#include <main.h>
#include <spi.h>

typedef struct
{
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint8_t bus; /* 共用同一条总线的设备 bus 相同 */
} SpiDeviceMap;

enum
{
    BUS_SPI2,
    BUS_COUNT,
};

static const SpiDeviceMap devices[SPI_DEV_COUNT] = {
    [SPI_DEV_IMU_ACCEL] = { &hspi2, ACCEL_CS_GPIO_Port, ACCEL_CS_Pin, BUS_SPI2 },
    [SPI_DEV_IMU_GYRO] = { &hspi2, GYRO_CS_GPIO_Port, GYRO_CS_Pin, BUS_SPI2 },
};

static volatile bool bus_busy[BUS_COUNT];

bool spi_select(SpiDevice dev)
{
    const SpiDeviceMap *d = &devices[dev];

    /* 查忙和置忙之间不能被打断（另一个任务可能同时来抢），短暂关中断 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const bool was_busy = bus_busy[d->bus];
    bus_busy[d->bus] = true;
    __set_PRIMASK(primask);

    if (was_busy)
    {
        return false;
    }
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET);
    return true;
}

void spi_deselect(SpiDevice dev)
{
    const SpiDeviceMap *d = &devices[dev];
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET);
    bus_busy[d->bus] = false;
}

bool spi_transfer(SpiDevice dev, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    /* HAL 的发送参数不是 const，但不会修改发送缓冲 */
    return HAL_SPI_TransmitReceive(devices[dev].hspi, (uint8_t *)tx, rx, len, SPI_TIMEOUT_MS)
           == HAL_OK;
}
