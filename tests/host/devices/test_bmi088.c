/**
 * @file    test_bmi088.c
 * @brief   bmi088 的单元测试：初始化与故障、读数换算（含加速度计 dummy 字节）、零偏、加热
 * @note    假 SPI 按数据手册时序模拟两颗芯片（fake_spi.h），驱动漏掉或多加 dummy 字节时读数会错位
 */
#include "devices/imu/bmi088.h"

#include "fake_pwm.h"
#include "fake_spi.h"
#include "fake_time.h"
#include "platform/time.h"
#include "unity.h"

#define GYRO_LSB  (2000.0f / 32768.0f * 3.14159265358979f / 180.0f)
#define ACCEL_LSB (6.0f * 9.8f / 32768.0f)

static Bmi088 imu;

void setUp(void)
{
    fake_spi_reset();
    fake_pwm_reset();
    fake_time_set_us(0u);
}

void tearDown(void)
{
}

static void set16(SpiDevice dev, uint8_t reg, int16_t v)
{
    fake_spi_set_reg(dev, reg, (uint8_t)((uint16_t)v & 0xFFu));
    fake_spi_set_reg(dev, (uint8_t)(reg + 1u), (uint8_t)((uint16_t)v >> 8));
}

static void test_init_writes_config(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    TEST_ASSERT_EQUAL_HEX8(0x04, fake_spi_get_reg(SPI_DEV_IMU_ACCEL, 0x7D));
    TEST_ASSERT_EQUAL_HEX8(0xAC, fake_spi_get_reg(SPI_DEV_IMU_ACCEL, 0x40));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_spi_get_reg(SPI_DEV_IMU_ACCEL, 0x41)); /* ±6 g */
    TEST_ASSERT_EQUAL_HEX8(0x00, fake_spi_get_reg(SPI_DEV_IMU_GYRO, 0x0F));  /* ±2000 °/s */
    TEST_ASSERT_EQUAL_HEX8(0x81, fake_spi_get_reg(SPI_DEV_IMU_GYRO, 0x10));
    TEST_ASSERT_EQUAL_HEX8(0x01, fake_spi_get_reg(SPI_DEV_IMU_GYRO, 0x18));
    TEST_ASSERT_TRUE(fake_pwm_started(PWM_IMU_HEATER));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_IMU_HEATER));
    TEST_ASSERT_FALSE(fake_spi_both_selected_seen());
    TEST_ASSERT_TRUE(rm_time_now_us() >= 160000u); /* 两次软复位各等 80 ms */
}

static void test_init_reports_missing_chip(void)
{
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x00, 0x00);
    TEST_ASSERT_EQUAL_INT(BMI088_ACCEL_NOT_FOUND, bmi088_init(&imu));
    fake_spi_reset();
    fake_spi_set_reg(SPI_DEV_IMU_GYRO, 0x00, 0x00);
    TEST_ASSERT_EQUAL_INT(BMI088_GYRO_NOT_FOUND, bmi088_init(&imu));
}

static void test_init_reports_config_readback_failure(void)
{
    fake_spi_make_read_only(SPI_DEV_IMU_ACCEL, 0x41);
    TEST_ASSERT_EQUAL_INT(BMI088_ACCEL_CONFIG_FAILED, bmi088_init(&imu));
    fake_spi_reset();
    fake_spi_make_read_only(SPI_DEV_IMU_GYRO, 0x10);
    TEST_ASSERT_EQUAL_INT(BMI088_GYRO_CONFIG_FAILED, bmi088_init(&imu));
}

static void test_read_converts_units(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    set16(SPI_DEV_IMU_ACCEL, 0x12, 1000);
    set16(SPI_DEV_IMU_ACCEL, 0x14, -1000);
    set16(SPI_DEV_IMU_ACCEL, 0x16, 5461);            /* 约 1 g */
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x22, 0x11); /* 温度原始值 136 → 40 °C */
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x23, 0x00);
    set16(SPI_DEV_IMU_GYRO, 0x02, 16384); /* 1000 °/s */
    set16(SPI_DEV_IMU_GYRO, 0x04, -16384);
    set16(SPI_DEV_IMU_GYRO, 0x06, 1);

    Bmi088Sample s;
    TEST_ASSERT_TRUE(bmi088_read(&imu, &s));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1000.0f * ACCEL_LSB, s.accel_m_s2[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1000.0f * ACCEL_LSB, s.accel_m_s2[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 9.8f, s.accel_m_s2[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 17.4533f, s.gyro_rad_s[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, -17.4533f, s.gyro_rad_s[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, GYRO_LSB, s.gyro_rad_s[2]);
    TEST_ASSERT_EQUAL_FLOAT(40.0f, s.temperature_c);
}

/* 11 位补码：原始值 2000 = -48 → 23 - 6 = 17 °C */
static void test_negative_temperature_code(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x22, 0xFA);
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x23, 0x00);
    Bmi088Sample s;
    TEST_ASSERT_TRUE(bmi088_read(&imu, &s));
    TEST_ASSERT_EQUAL_FLOAT(17.0f, s.temperature_c);
}

static void test_bad_gyro_id_invalidates_frame(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    fake_spi_set_reg(SPI_DEV_IMU_GYRO, 0x00, 0xFF);
    Bmi088Sample s;
    TEST_ASSERT_FALSE(bmi088_read(&imu, &s));
}

static void test_gyro_offset_subtracted(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    set16(SPI_DEV_IMU_GYRO, 0x02, 100);
    const float offset[3] = { 100.0f * GYRO_LSB, 0.01f, -0.01f };
    bmi088_set_gyro_offset(&imu, offset);
    Bmi088Sample s;
    TEST_ASSERT_TRUE(bmi088_read(&imu, &s));
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.0f, s.gyro_rad_s[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, -0.01f, s.gyro_rad_s[1]);
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, 0.01f, s.gyro_rad_s[2]);
}

/* 调用 n 次加热（1 kHz 下即 n ms） */
static void heat(int n, float temperature_c)
{
    for (int i = 0; i < n; i++)
    {
        bmi088_heater_step(&imu, temperature_c);
    }
}

/*
 * UniC 参数：每 100 次算一次；kp 0.05 / °C，ki 每次 0.00025 / °C，上限 25%。
 * 低 1 °C：第一次 0.05 + 0.00025 = 0.05025；低 20 °C 截到 0.25
 */
static void test_heater_unic_params(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    heat(99, 39.0f);
    TEST_ASSERT_EQUAL_UINT(0u, fake_pwm_set_count(PWM_IMU_HEATER));
    heat(1, 39.0f);
    TEST_ASSERT_EQUAL_UINT(1u, fake_pwm_set_count(PWM_IMU_HEATER));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.05025f, fake_pwm_duty(PWM_IMU_HEATER));
    heat(100, 39.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0505f, fake_pwm_duty(PWM_IMU_HEATER)); /* 积分在累加 */

    heat(100, 20.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, fake_pwm_duty(PWM_IMU_HEATER));
}

/* 长时间低 0.1 °C：占空比不超过 25%；抗积分饱和（ADR 0040）使积分只攒到“比例 + 积分 = 上限”为止，
 * 即 (0.25 − 0.005) / ki = 980，而不是积分限幅 1000 */
static void test_heater_integral_bounded(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    heat(100 * 20000, 39.9f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, fake_pwm_duty(PWM_IMU_HEATER));
    TEST_ASSERT_FLOAT_WITHIN(1e-1f, 980.0f, imu.heater_pid.integral);
}

/* 冷启动预热后到达目标：积分没有攒满，过了 40 °C 占空比马上降下来（旧行为会顶在 25% 很久，造成过冲） */
static void test_heater_no_windup_after_warmup(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    heat(100 * 600, 25.0f); /* 冷态 60 s，比例项一直饱和 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, fake_pwm_duty(PWM_IMU_HEATER));
    heat(100, 40.5f); /* 刚过目标 */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, fake_pwm_duty(PWM_IMU_HEATER));
}

/* 过热时输出为负：占空比为 0，而不是旧代码转成 uint16_t 后的满占空比 */
static void test_heater_overtemperature_gives_zero_duty(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    heat(100, 45.0f);
    TEST_ASSERT_EQUAL_UINT(1u, fake_pwm_set_count(PWM_IMU_HEATER));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_IMU_HEATER));
}

/* 读失败时关加热但保留积分：下一次读到温度就回到原来的占空比，不用重新攒积分 */
static void test_heater_off(void)
{
    TEST_ASSERT_EQUAL_INT(BMI088_OK, bmi088_init(&imu));
    heat(100 * 3000, 39.9f); /* 稳态附近 5 分钟，积分已攒起来 */
    const float integral = imu.heater_pid.integral;
    const float duty = fake_pwm_duty(PWM_IMU_HEATER);
    TEST_ASSERT_TRUE(duty > 0.05f);
    bmi088_heater_off(&imu);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_IMU_HEATER));
    TEST_ASSERT_EQUAL_FLOAT(integral, imu.heater_pid.integral);
    heat(100, 39.9f);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, duty, fake_pwm_duty(PWM_IMU_HEATER)); /* 回到原来的占空比 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_writes_config);
    RUN_TEST(test_init_reports_missing_chip);
    RUN_TEST(test_init_reports_config_readback_failure);
    RUN_TEST(test_read_converts_units);
    RUN_TEST(test_negative_temperature_code);
    RUN_TEST(test_bad_gyro_id_invalidates_frame);
    RUN_TEST(test_gyro_offset_subtracted);
    RUN_TEST(test_heater_unic_params);
    RUN_TEST(test_heater_integral_bounded);
    RUN_TEST(test_heater_no_windup_after_warmup);
    RUN_TEST(test_heater_overtemperature_gives_zero_duty);
    RUN_TEST(test_heater_off);
    return UNITY_END();
}
