/**
 * @file    test_snapshot.c
 * @brief   snapshot 的单元测试：从未写入、最新值覆盖、新旧判断的边界、失败时不改 out
 */
#include "04_core/util/snapshot.h"

#include "fake_time.h"
#include "unity.h"

typedef struct
{
    int a;
    float b;
} Sample;

static Snapshot snap;
static Sample slot;

void setUp(void)
{
    snap = (Snapshot){ 0 };
    slot = (Sample){ 0 };
    fake_time_set_us(1000000u);
}

void tearDown(void)
{
}

static bool read_sample(Sample *out, uint32_t max_age_ms)
{
    return snapshot_read(&snap, &slot, out, sizeof(*out), max_age_ms);
}

static void write_sample(int a, float b)
{
    const Sample s = { a, b };
    snapshot_write(&snap, &slot, &s, sizeof(s));
}

static void test_never_written_reads_false(void)
{
    Sample out;
    TEST_ASSERT_FALSE(read_sample(&out, SNAPSHOT_ANY_AGE));
}

static void test_latest_value_wins(void)
{
    Sample out;
    write_sample(1, 1.5f);
    write_sample(2, 2.5f);
    TEST_ASSERT_TRUE(read_sample(&out, SNAPSHOT_ANY_AGE));
    TEST_ASSERT_EQUAL_INT(2, out.a);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, out.b);
}

/* 正好 max_age 算新，再多 1 µs 算旧 */
static void test_age_boundary(void)
{
    Sample out;
    write_sample(7, 0.0f);
    fake_time_advance_ms(200u);
    TEST_ASSERT_TRUE(read_sample(&out, 200u));
    fake_time_set_us(1000000u + 200000u + 1u);
    TEST_ASSERT_FALSE(read_sample(&out, 200u));
    TEST_ASSERT_TRUE(read_sample(&out, SNAPSHOT_ANY_AGE));
}

static void test_failed_read_leaves_out_untouched(void)
{
    Sample out = { 42, 4.2f };
    write_sample(1, 1.0f);
    fake_time_advance_ms(500u);
    TEST_ASSERT_FALSE(read_sample(&out, 100u));
    TEST_ASSERT_EQUAL_INT(42, out.a);
}

/* 读取方取完时刻后恰好有新的写入：时间戳比读取时刻还新，按“刚写入”处理 */
static void test_stamp_newer_than_reader_is_fresh(void)
{
    Sample out;
    fake_time_set_us(2000000u);
    write_sample(3, 0.0f);
    fake_time_set_us(1999000u);
    TEST_ASSERT_TRUE(read_sample(&out, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_never_written_reads_false);
    RUN_TEST(test_latest_value_wins);
    RUN_TEST(test_age_boundary);
    RUN_TEST(test_failed_read_leaves_out_untouched);
    RUN_TEST(test_stamp_newer_than_reader_is_fresh);
    return UNITY_END();
}
