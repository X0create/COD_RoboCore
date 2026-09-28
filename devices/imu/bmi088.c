/**
 * @file    bmi088.c
 * @brief   BMI088 与加热恒温，见 bmi088.h
 */
#include "bmi088.h"

#include "core/os/delay.h"
#include "platform/pwm.h"
#include "platform/spi.h"

/* ---- 寄存器（BMI088 数据手册；取值同 COD-H7-Template Bmi088_Reg.h） ---- */
#define ACC_CHIP_ID         0x00u
#define ACC_CHIP_ID_VALUE   0x1Eu
#define ACC_XOUT_L          0x12u
#define ACC_TEMP_MSB        0x22u
#define ACC_CONF            0x40u
#define ACC_RANGE           0x41u
#define ACC_INT1_IO_CTRL    0x53u
#define ACC_INT_MAP_DATA    0x58u
#define ACC_PWR_CONF        0x7Cu
#define ACC_PWR_CTRL        0x7Du
#define ACC_SOFTRESET       0x7Eu
#define GYRO_CHIP_ID        0x00u
#define GYRO_CHIP_ID_VALUE  0x0Fu
#define GYRO_RANGE          0x0Fu
#define GYRO_BANDWIDTH      0x10u
#define GYRO_LPM1           0x11u
#define GYRO_SOFTRESET      0x14u
#define GYRO_INT_CTRL       0x15u
#define GYRO_INT3_INT4_CONF 0x16u
#define GYRO_INT3_INT4_MAP  0x18u
#define SOFTRESET_VALUE     0xB6u
#define READ_BIT            0x80u

#define RESET_WAIT_MS 80u  /* 软复位后等待 */
#define REG_WAIT_US   150u /* 相邻两次寄存器访问的间隔 */

/* ---- 换算系数（同旧工程） ---- */
#define ACCEL_6G_M_S2_PER_LSB   (6.0f * 9.8f / 32768.0f)
#define GYRO_2000_RAD_S_PER_LSB (2000.0f / 32768.0f * 3.14159265358979f / 180.0f)
#define TEMP_C_PER_LSB          0.125f
#define TEMP_OFFSET_C           23.0f

/*
 * ---- 加热（UniC 在同款 MC02 上实测的参数，ADR 0033）----
 * 加热片每 1% 占空比约换来 0.28 °C；旧工程 10% 上限、积分不起作用，到不了 40 °C。
 * UniC 把上限提到 25%、打开积分，实测稳态 40.000–40.125 °C、占空比约 16.4%（UniC `imu-heater-authority`）：
 * - 上限 25%；kp = 上限 / 5 °C（误差 5 °C 时比例项刚好饱和）；ki = 上限 / 100，单位 1 / (°C·s)；
 * - 每 100 ms 算一次（BMI088 温度寄存器本身更新就慢）。
 * 本模板 PID 不带 dt（ADR 0029），ki 乘以周期换成每次的增益；积分限幅取“积分项最多等于上限”。
 * UniC 的 PID 用反算法抗积分饱和，这里没有，预热到 40 °C 时过冲可能比 UniC 大（V6 实测）。
 */
#define HEATER_PERIOD_MS 100u
#define HEATER_DUTY_CAP  0.25f
#define HEATER_KI_STEP   (HEATER_DUTY_CAP / 100.0f * ((float)HEATER_PERIOD_MS / 1000.0f))
#define HEATER_PID_PARAM                                                                           \
    ((PidParam){ .kp = HEATER_DUTY_CAP / 5.0f,                                                     \
                 .ki = HEATER_KI_STEP,                                                             \
                 .integral_limit = HEATER_DUTY_CAP / HEATER_KI_STEP,                               \
                 .output_limit = HEATER_DUTY_CAP })

typedef struct
{
    uint8_t reg;
    uint8_t value;
} RegValue;

/* 配置顺序同旧工程；每项写入后读回核对 */
static const RegValue accel_config[] = {
    { ACC_PWR_CTRL, 0x04u },     /* 打开加速度计 */
    { ACC_PWR_CONF, 0x00u },     /* 活动模式 */
    { ACC_CONF, 0xACu },         /* 必须置位 0x80 | 正常带宽 0x20 | 1600 Hz 0x0C */
    { ACC_RANGE, 0x01u },        /* ±6 g */
    { ACC_INT1_IO_CTRL, 0x08u }, /* INT1 输出使能、推挽、低有效 */
    { ACC_INT_MAP_DATA, 0x04u }, /* 数据就绪映射到 INT1 */
};

static const RegValue gyro_config[] = {
    { GYRO_RANGE, 0x00u },          /* ±2000 °/s */
    { GYRO_BANDWIDTH, 0x81u },      /* 必须置位 0x80 | 2000 Hz 输出、230 Hz 带宽 */
    { GYRO_LPM1, 0x00u },           /* 正常模式 */
    { GYRO_INT_CTRL, 0x80u },       /* 数据就绪中断打开 */
    { GYRO_INT3_INT4_CONF, 0x00u }, /* INT3 推挽、低有效 */
    { GYRO_INT3_INT4_MAP, 0x01u },  /* 数据就绪映射到 INT3 */
};

/* 加速度计读数在地址之后有一个 dummy 字节，陀螺仪没有（数据手册 SPI 读时序） */
static uint8_t dummy_bytes(SpiDevice dev)
{
    return (dev == SPI_DEV_IMU_ACCEL) ? 1u : 0u;
}

/* 一个完整事务：选中 → 地址 + [dummy] + len 个数据 → 释放 */
static bool read_regs(SpiDevice dev, uint8_t reg, uint8_t *out, uint8_t len)
{
    uint8_t tx[12] = { 0 };
    uint8_t rx[12];
    const uint8_t skip = (uint8_t)(1u + dummy_bytes(dev));
    tx[0] = (uint8_t)(reg | READ_BIT);

    if (!spi_select(dev))
    {
        return false;
    }
    const bool ok = spi_transfer(dev, tx, rx, (uint16_t)(skip + len));
    spi_deselect(dev);

    for (uint8_t i = 0u; i < len; i++)
    {
        out[i] = rx[skip + i];
    }
    return ok;
}

static bool write_reg(SpiDevice dev, uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = { reg, value };
    uint8_t rx[2];
    if (!spi_select(dev))
    {
        return false;
    }
    const bool ok = spi_transfer(dev, tx, rx, 2u);
    spi_deselect(dev);
    return ok;
}

/* 读两次芯片 ID（加速度计上电后第一次读用来切换到 SPI 模式），返回第二次的值 */
static uint8_t read_chip_id(SpiDevice dev)
{
    uint8_t id = 0u;
    (void)read_regs(dev, ACC_CHIP_ID, &id, 1u); /* 两颗芯片的 ID 寄存器地址都是 0x00 */
    rm_delay_us(REG_WAIT_US);
    (void)read_regs(dev, ACC_CHIP_ID, &id, 1u);
    rm_delay_us(REG_WAIT_US);
    return id;
}

static bool reset_and_check(SpiDevice dev, uint8_t reset_reg, uint8_t expect_id)
{
    (void)read_chip_id(dev);
    (void)write_reg(dev, reset_reg, SOFTRESET_VALUE);
    rm_delay_ms(RESET_WAIT_MS);
    return read_chip_id(dev) == expect_id;
}

static bool configure(SpiDevice dev, const RegValue *table, uint32_t count)
{
    for (uint32_t i = 0u; i < count; i++)
    {
        uint8_t readback = 0u;
        const bool ok = write_reg(dev, table[i].reg, table[i].value);
        rm_delay_us(REG_WAIT_US);
        const bool read_ok = read_regs(dev, table[i].reg, &readback, 1u);
        rm_delay_us(REG_WAIT_US);
        if (!ok || !read_ok || readback != table[i].value)
        {
            return false;
        }
    }
    return true;
}

Bmi088Status bmi088_init(Bmi088 *imu)
{
    const PidParam heater_param = HEATER_PID_PARAM;
    *imu = (Bmi088){ 0 };
    pid_init(&imu->heater_pid, PID_POSITION, &heater_param);

    if (!reset_and_check(SPI_DEV_IMU_ACCEL, ACC_SOFTRESET, ACC_CHIP_ID_VALUE))
    {
        return BMI088_ACCEL_NOT_FOUND;
    }
    if (!configure(SPI_DEV_IMU_ACCEL, accel_config, sizeof(accel_config) / sizeof(accel_config[0])))
    {
        return BMI088_ACCEL_CONFIG_FAILED;
    }
    if (!reset_and_check(SPI_DEV_IMU_GYRO, GYRO_SOFTRESET, GYRO_CHIP_ID_VALUE))
    {
        return BMI088_GYRO_NOT_FOUND;
    }
    if (!configure(SPI_DEV_IMU_GYRO, gyro_config, sizeof(gyro_config) / sizeof(gyro_config[0])))
    {
        return BMI088_GYRO_CONFIG_FAILED;
    }
    if (!pwm_start(PWM_IMU_HEATER))
    {
        return BMI088_HEATER_START_FAILED;
    }
    return BMI088_OK;
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

bool bmi088_read(Bmi088 *imu, Bmi088Sample *out)
{
    uint8_t acc[6];
    uint8_t temp[2];
    uint8_t gyro[8];

    if (!read_regs(SPI_DEV_IMU_ACCEL, ACC_XOUT_L, acc, 6u)
        || !read_regs(SPI_DEV_IMU_ACCEL, ACC_TEMP_MSB, temp, 2u)
        || !read_regs(SPI_DEV_IMU_GYRO, GYRO_CHIP_ID, gyro, 8u))
    {
        return false;
    }
    /* 从 ID 寄存器连读：[0] ID、[1] 保留、[2..7] 三轴角速度。ID 不对说明这次传输不可信 */
    if (gyro[0] != GYRO_CHIP_ID_VALUE)
    {
        return false;
    }

    for (int i = 0; i < 3; i++)
    {
        out->accel_m_s2[i] = ACCEL_6G_M_S2_PER_LSB * (float)le16(&acc[2 * i]);
        out->gyro_rad_s[i] =
            GYRO_2000_RAD_S_PER_LSB * (float)le16(&gyro[2 + 2 * i]) - imu->gyro_offset_rad_s[i];
    }
    /* 温度 11 位补码：高 8 位在 MSB，低 3 位在 LSB 的最高 3 位 */
    int16_t t = (int16_t)(((uint16_t)temp[0] << 3) | (temp[1] >> 5));
    if (t > 1023)
    {
        t = (int16_t)(t - 2048);
    }
    out->temperature_c = (float)t * TEMP_C_PER_LSB + TEMP_OFFSET_C;
    return true;
}

void bmi088_set_gyro_offset(Bmi088 *imu, const float offset_rad_s[3])
{
    for (int i = 0; i < 3; i++)
    {
        imu->gyro_offset_rad_s[i] = offset_rad_s[i];
    }
}

void bmi088_heater_step(Bmi088 *imu, float temperature_c)
{
    if (++imu->heater_tick < HEATER_PERIOD_MS)
    {
        return;
    }
    imu->heater_tick = 0u;
    /* PID 输出在 ±上限之间；负数（过热）由 pwm_set_duty 截到 0，不会变成满占空比 */
    pwm_set_duty(PWM_IMU_HEATER, pid_calc(&imu->heater_pid, BMI088_HEATER_TARGET_C, temperature_c));
}

void bmi088_heater_off(Bmi088 *imu)
{
    pid_reset(&imu->heater_pid);
    pwm_set_duty(PWM_IMU_HEATER, 0.0f);
}
