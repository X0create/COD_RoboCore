/**
 * @file    test_buzzer.c
 * @brief   buzzer 的单元测试：音符的频率与时长、静音段、播放结束、打断
 */
#include "devices/buzzer/buzzer.h"

#include "fake_pwm.h"
#include "unity.h"

static Buzzer bz;

void setUp(void)
{
    fake_pwm_reset();
    TEST_ASSERT_TRUE(buzzer_init(&bz));
}

void tearDown(void)
{
}

/* 解锁音：1568 Hz 80 ms → 静音 20 ms → 2093 Hz 80 ms → 结束 */
static void test_arm_sequence(void)
{
    TEST_ASSERT_TRUE(fake_pwm_started(PWM_BUZZER));
    buzzer_play(&bz, BUZZER_ARM);
    TEST_ASSERT_EQUAL_FLOAT(1568.0f, fake_pwm_frequency(PWM_BUZZER));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, fake_pwm_duty(PWM_BUZZER));

    buzzer_step(&bz, 50u);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, fake_pwm_duty(PWM_BUZZER));
    buzzer_step(&bz, 30u);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_BUZZER));
    buzzer_step(&bz, 20u);
    TEST_ASSERT_EQUAL_FLOAT(2093.0f, fake_pwm_frequency(PWM_BUZZER));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, fake_pwm_duty(PWM_BUZZER));
    buzzer_step(&bz, 80u);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_BUZZER));
    TEST_ASSERT_FALSE(bz.active);
}

/* 播放完不再动 PWM */
static void test_idle_does_nothing(void)
{
    const unsigned before = fake_pwm_set_count(PWM_BUZZER);
    buzzer_step(&bz, 1000u);
    TEST_ASSERT_EQUAL_UINT(before, fake_pwm_set_count(PWM_BUZZER));
}

/* 新的提示音打断正在播放的 */
static void test_play_interrupts(void)
{
    buzzer_play(&bz, BUZZER_LOW_BATTERY);
    buzzer_step(&bz, 50u);
    buzzer_play(&bz, BUZZER_DISARM);
    TEST_ASSERT_EQUAL_FLOAT(2093.0f, fake_pwm_frequency(PWM_BUZZER));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, fake_pwm_duty(PWM_BUZZER));
}

/* 以 25 ms 的步长推进（样板的心跳周期），启动音最终播完且静音 */
static void test_startup_finishes_with_coarse_steps(void)
{
    buzzer_play(&bz, BUZZER_STARTUP);
    for (int i = 0; i < 40 && bz.active; i++)
    {
        buzzer_step(&bz, 25u);
    }
    TEST_ASSERT_FALSE(bz.active);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fake_pwm_duty(PWM_BUZZER));
    TEST_ASSERT_EQUAL_FLOAT(784.0f, fake_pwm_frequency(PWM_BUZZER));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arm_sequence);
    RUN_TEST(test_idle_does_nothing);
    RUN_TEST(test_play_interrupts);
    RUN_TEST(test_startup_finishes_with_coarse_steps);
    return UNITY_END();
}
