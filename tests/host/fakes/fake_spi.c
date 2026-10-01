/**
 * @file    fake_spi.c
 * @brief   假 SPI + BMI088 寄存器模型，见 fake_spi.h；代替 05_platform/spi/spi_stm32h7.c
 */
#include "fake_spi.h"

#include <string.h>

static uint8_t regs[SPI_DEV_COUNT][128];
static bool read_only[SPI_DEV_COUNT][128];
static bool selected[SPI_DEV_COUNT];
static bool both_seen;

void fake_spi_reset(void)
{
    memset(regs, 0, sizeof(regs));
    memset(read_only, 0, sizeof(read_only));
    memset(selected, 0, sizeof(selected));
    both_seen = false;
    regs[SPI_DEV_IMU_ACCEL][0x00] = 0x1E;
    regs[SPI_DEV_IMU_GYRO][0x00] = 0x0F;
}

void fake_spi_set_reg(SpiDevice dev, uint8_t reg, uint8_t value)
{
    regs[dev][reg & 0x7Fu] = value;
}

uint8_t fake_spi_get_reg(SpiDevice dev, uint8_t reg)
{
    return regs[dev][reg & 0x7Fu];
}

void fake_spi_make_read_only(SpiDevice dev, uint8_t reg)
{
    read_only[dev][reg & 0x7Fu] = true;
}

bool fake_spi_both_selected_seen(void)
{
    return both_seen;
}

bool spi_select(SpiDevice dev)
{
    for (int d = 0; d < (int)SPI_DEV_COUNT; d++)
    {
        if (selected[d])
        {
            both_seen = true;
            return false; /* 同一条总线已被占用 */
        }
    }
    selected[dev] = true;
    return true;
}

void spi_deselect(SpiDevice dev)
{
    selected[dev] = false;
}

bool spi_transfer(SpiDevice dev, const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    const uint8_t addr = tx[0] & 0x7Fu;
    const bool is_read = (tx[0] & 0x80u) != 0u;
    const uint16_t first_data = (dev == SPI_DEV_IMU_ACCEL) ? 2u : 1u; /* 加速度计有 dummy 字节 */

    rx[0] = 0xFFu;
    for (uint16_t i = 1u; i < len; i++)
    {
        if (is_read)
        {
            rx[i] = (i < first_data) ? 0xA5u /* dummy：无意义的值 */
                                     : regs[dev][(uint8_t)(addr + (i - first_data)) & 0x7Fu];
        }
        else
        {
            rx[i] = 0xFFu;
            if (i == 1u && !read_only[dev][addr])
            {
                regs[dev][addr] = tx[1];
            }
        }
    }
    return true;
}
