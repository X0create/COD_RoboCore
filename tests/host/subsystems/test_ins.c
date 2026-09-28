/**
 * @file    test_ins.c
 * @brief   ins 的单元测试：标定前不发布、标定通过后发布、在动时拒绝并重试、安装旋转、坏帧处理
 * @note    BMI088 由假 SPI 的寄存器模型提供数据（fake_spi.h）
 */
#include "subsystems/ins/ins.h"

#include "fake_pwm.h"
#include "fake_spi.h"
#include "fake_time.h"
#include "unity.h"

#define GYRO_LSB (2000.0f / 32768.0f * 3.14159265358979f / 180.0f)

static const InsConfig identity = { .install_rotation = { 1, 0, 0, 0, 1, 0, 0, 0, 1 } };
/* 芯片 x 轴朝机体 y（左）、芯片 y 轴朝机体 -x：绕 z 转 90° */
static const InsConfig rot_z90 = { .install_rotation = { 0, -1, 0, 1, 0, 0, 0, 0, 1 } };

static Ins ins;
static ImuStateTopic topic;

static void set16(SpiDevice dev, uint8_t reg, int16_t v)
{
    fake_spi_set_reg(dev, reg, (uint8_t)((uint16_t)v & 0xFFu));
    fake_spi_set_reg(dev, (uint8_t)(reg + 1u), (uint8_t)((uint16_t)v >> 8));
}

/* 板子水平静止：加速度 z ≈ 1 g，温度 40 °C，陀螺为给定原始值 */
static void sensor(int16_t gx, int16_t gy, int16_t gz)
{
    set16(SPI_DEV_IMU_ACCEL, 0x12, 0);
    set16(SPI_DEV_IMU_ACCEL, 0x14, 0);
    set16(SPI_DEV_IMU_ACCEL, 0x16, 5461);
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x22, 0x11);
    fake_spi_set_reg(SPI_DEV_IMU_ACCEL, 0x23, 0x00);
    set16(SPI_DEV_IMU_GYRO, 0x02, gx);
    set16(SPI_DEV_IMU_GYRO, 0x04, gy);
    set16(SPI_DEV_IMU_GYRO, 0x06, gz);
}

static void start(const InsConfig *cfg)
{
    topic = (ImuStateTopic){ 0 };
    TEST_ASSERT_TRUE(ins_init(&ins, cfg, &topic));
    TEST_ASSERT_EQUAL_INT(BMI088_OK, ins_start(&ins));
}

/* 跑 n 步（每步假时钟前进 1 ms），返回最后一个非 NONE 事件 */
static InsEvent steps(uint32_t n)
{
    InsEvent last = INS_EVENT_NONE;
    for (uint32_t i = 0u; i < n; i++)
    {
        fake_time_advance_ms(1u);
        const InsEvent ev = ins_step(&ins);
        if (ev != INS_EVENT_NONE)
        {
            last = ev;
        }
    }
    return last;
}

void setUp(void)
{
    fake_spi_reset();
    fake_pwm_reset();
    fake_time_set_us(0u);
}

void tearDown(void)
{
}

/* 标定期间不发布；满 2000 个样本后采用零偏、开始发布，发布的角速度已减零偏 */
static void test_publishes_only_after_calibration(void)
{
    start(&identity);
    sensor(10, -5, 3); /* 零偏约 0.01 rad/s 量级 */
    TEST_ASSERT_EQUAL_INT(INS_EVENT_NONE, steps(INS_CALIB_SAMPLES - 1u));
    ImuState st;
    TEST_ASSERT_FALSE(imu_state_read(&topic, &st, TOPIC_ANY_AGE));

    TEST_ASSERT_EQUAL_INT(INS_EVENT_CALIBRATED, steps(1u));
    steps(1u);
    TEST_ASSERT_TRUE(imu_state_read(&topic, &st, IMU_STALE_MS));
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, st.gyro_rad_s[i]);
    }
    TEST_ASSERT_EQUAL_FLOAT(40.0f, st.temperature_c);
}

/* 标定期间在动（角速度大幅变化）：拒绝并重新采样，静止后通过 */
static void test_moving_rejects_then_retries(void)
{
    start(&identity);
    for (uint32_t i = 0u; i < INS_CALIB_SAMPLES; i++)
    {
        sensor((i % 2u) ? 300 : -300, 0, 0); /* ±0.32 rad/s 来回 */
        fake_time_advance_ms(1u);
        const InsEvent ev = ins_step(&ins);
        if (i + 1u == INS_CALIB_SAMPLES)
        {
            TEST_ASSERT_EQUAL_INT(INS_EVENT_CALIB_NOT_STILL, ev);
        }
    }
    ImuState st;
    TEST_ASSERT_FALSE(imu_state_read(&topic, &st, TOPIC_ANY_AGE));

    sensor(0, 0, 0);
    TEST_ASSERT_EQUAL_INT(INS_EVENT_CALIBRATED, steps(INS_CALIB_SAMPLES));
}

/* 匀速转（均值约 0.5 rad/s）：均值过大，拒绝 */
static void test_steady_rotation_rejected(void)
{
    start(&identity);
    sensor(0, 0, 470);
    TEST_ASSERT_EQUAL_INT(INS_EVENT_CALIB_BIAS_TOO_LARGE, steps(INS_CALIB_SAMPLES));
}

/* 安装旋转：芯片绕 x 的角速度在机体系里是绕 y */
static void test_install_rotation_applied(void)
{
    start(&rot_z90);
    sensor(0, 0, 0);
    steps(INS_CALIB_SAMPLES + 1u);
    sensor(100, 0, 0);
    steps(1u);
    ImuState st;
    TEST_ASSERT_TRUE(imu_state_read(&topic, &st, IMU_STALE_MS));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, st.gyro_rad_s[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 100.0f * GYRO_LSB, st.gyro_rad_s[1]);
}

/* 读失败或加速度全零：不发布、关加热；话题随之过期（安全门据此全车停） */
static void test_bad_frames_stop_publishing(void)
{
    start(&identity);
    sensor(0, 0, 0);
    steps(INS_CALIB_SAMPLES + 1u);
    set16(SPI_DEV_IMU_ACCEL, 0x16, 0); /* 加速度全零 */
    TEST_ASSERT_EQUAL_INT(INS_EVENT_READ_FAILED, steps(IMU_STALE_MS + 1u));
    ImuState st;
    TEST_ASSERT_FALSE(imu_state_read(&topic, &st, IMU_STALE_MS));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_IMU_HEATER));

    fake_spi_set_reg(SPI_DEV_IMU_GYRO, 0x00, 0xFF); /* 陀螺 ID 错 */
    TEST_ASSERT_EQUAL_INT(INS_EVENT_READ_FAILED, steps(1u));
}

/* EKF 用实测间隔：每 2 ms 更新一次、绕 z 约 1 rad/s，0.5 s 后航向约 0.5 rad（固定 1 ms 会只有一半） */
static void test_uses_measured_dt(void)
{
    start(&identity);
    sensor(0, 0, 0);
    steps(INS_CALIB_SAMPLES + 1u);
    sensor(0, 0, 939); /* 939 × 2000/32768 °/s ≈ 1.000 rad/s */
    const float rate = 939.0f * GYRO_LSB;
    float t_s = 0.0f;
    for (int i = 0; i < 250; i++)
    {
        fake_time_advance_ms(2u);
        TEST_ASSERT_EQUAL_INT(INS_EVENT_NONE, ins_step(&ins));
        t_s += 0.002f;
    }
    ImuState st;
    TEST_ASSERT_TRUE(imu_state_read(&topic, &st, IMU_STALE_MS));
    /* 第一次更新用标称 1 ms，其余 249 次各 2 ms */
    TEST_ASSERT_FLOAT_WITHIN(5e-3f, rate * (t_s - 0.001f), st.yaw_rad);
}

/* 静止水平时发布的姿态接近水平 */
static void test_level_attitude(void)
{
    start(&identity);
    sensor(0, 0, 0);
    steps(INS_CALIB_SAMPLES + 3000u);
    ImuState st;
    TEST_ASSERT_TRUE(imu_state_read(&topic, &st, IMU_STALE_MS));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, st.pitch_rad);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 0.0f, st.roll_rad);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_publishes_only_after_calibration);
    RUN_TEST(test_moving_rejects_then_retries);
    RUN_TEST(test_steady_rotation_rejected);
    RUN_TEST(test_install_rotation_applied);
    RUN_TEST(test_bad_frames_stop_publishing);
    RUN_TEST(test_uses_measured_dt);
    RUN_TEST(test_level_attitude);
    return UNITY_END();
}
