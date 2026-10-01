/**
 * @file    test_cycle_extend.c
 * @brief   cycle_extend 的单元测试：正常前进、跨过回绕、连续多圈
 */
#include "cycle_extend.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_init_starts_from_raw(void)
{
    CycleExtender ext;
    cycle_extend_init(&ext, 1000u);
    TEST_ASSERT_EQUAL_UINT64(1000u, ext.total);
}

static void test_update_adds_elapsed_cycles(void)
{
    CycleExtender ext;
    cycle_extend_init(&ext, 1000u);
    TEST_ASSERT_EQUAL_UINT64(1500u, cycle_extend_update(&ext, 1500u));
    TEST_ASSERT_EQUAL_UINT64(1500u, cycle_extend_update(&ext, 1500u)); /* 没前进就不变 */
}

static void test_update_crosses_wrap(void)
{
    CycleExtender ext;
    cycle_extend_init(&ext, 0xFFFFFFF0u);
    /* 原始值回绕到 0x10：实际前进了 0x20 个周期 */
    TEST_ASSERT_EQUAL_UINT64(0x100000010ull, cycle_extend_update(&ext, 0x10u));
}

static void test_update_accumulates_many_wraps(void)
{
    CycleExtender ext;
    cycle_extend_init(&ext, 0u);
    /* 每次前进 3/4 圈，走 8 次 = 6 整圈 */
    const uint32_t step = 0xC0000000u;
    uint32_t raw = 0u;
    for (int i = 0; i < 8; i++)
    {
        raw += step;
        cycle_extend_update(&ext, raw);
    }
    TEST_ASSERT_EQUAL_UINT64(6ull << 32, ext.total);
}

static void test_update_max_step_just_below_one_wrap(void)
{
    CycleExtender ext;
    cycle_extend_init(&ext, 5u);
    /* 前进 2^32 - 1 个周期是 @pre 允许的最大值 */
    TEST_ASSERT_EQUAL_UINT64(5ull + 0xFFFFFFFFull, cycle_extend_update(&ext, 4u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_starts_from_raw);
    RUN_TEST(test_update_adds_elapsed_cycles);
    RUN_TEST(test_update_crosses_wrap);
    RUN_TEST(test_update_accumulates_many_wraps);
    RUN_TEST(test_update_max_step_just_below_one_wrap);
    return UNITY_END();
}
