/**
 * @file    test_battery.c
 * @brief   battery 的单元测试：分压换算、持续 1 s 才报低电量、短暂压降不误报、回差解除
 */
#include "02_devices/battery/battery.h"

#include "unity.h"

static const BatteryConfig cfg = {
    .divider = 11.0f, .low_v = 21.0f, .recover_v = 21.5f, .hold_ms = 1000u
};
static Battery bat;

void setUp(void)
{
    battery_init(&bat, &cfg);
}

void tearDown(void)
{
}

#define MS(x) ((uint64_t)(x) * 1000u)

static void test_voltage_conversion(void)
{
    TEST_ASSERT_FALSE(battery_update(&bat, 2.0f, 0u));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 22.0f, bat.voltage_v);
}

static void test_low_after_hold_time(void)
{
    TEST_ASSERT_FALSE(battery_update(&bat, 1.8f, MS(100))); /* 19.8 V */
    TEST_ASSERT_FALSE(battery_update(&bat, 1.8f, MS(1099)));
    TEST_ASSERT_TRUE(battery_update(&bat, 1.8f, MS(1100)));
}

/* 电机启动时压降 500 ms，又回到正常：不报 */
static void test_short_dip_ignored(void)
{
    (void)battery_update(&bat, 1.8f, MS(0));
    (void)battery_update(&bat, 1.8f, MS(500));
    TEST_ASSERT_FALSE(battery_update(&bat, 2.0f, MS(600)));
    TEST_ASSERT_FALSE(battery_update(&bat, 1.8f, MS(700))); /* 重新计时 */
    TEST_ASSERT_FALSE(battery_update(&bat, 1.8f, MS(1600)));
    TEST_ASSERT_TRUE(battery_update(&bat, 1.8f, MS(1700)));
}

/* 回差：回到 21.2 V 仍低电量，超过 21.5 V 才解除 */
static void test_recover_with_hysteresis(void)
{
    (void)battery_update(&bat, 1.8f, MS(0));
    TEST_ASSERT_TRUE(battery_update(&bat, 1.8f, MS(1000)));
    TEST_ASSERT_TRUE(battery_update(&bat, 21.2f / 11.0f, MS(1100)));
    TEST_ASSERT_FALSE(battery_update(&bat, 21.6f / 11.0f, MS(1200)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_voltage_conversion);
    RUN_TEST(test_low_after_hold_time);
    RUN_TEST(test_short_dip_ignored);
    RUN_TEST(test_recover_with_hysteresis);
    return UNITY_END();
}
