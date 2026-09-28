/**
 * @file    test_topic.c
 * @brief   topic 的单元测试：认领唯一、从未发布、最新值覆盖、新旧判断的边界、失败时不改 out
 */
#include "core/msg/topic.h"

#include "fake_time.h"
#include "unity.h"

typedef struct
{
    int a;
    float b;
} Sample;

typedef struct
{
    Topic base;
    Sample data;
} SampleTopic;

static SampleTopic topic;

void setUp(void)
{
    topic = (SampleTopic){ 0 };
    fake_time_set_us(1000000u);
}

void tearDown(void)
{
}

static bool read_sample(Sample *out, uint32_t max_age_ms)
{
    return topic_read(&topic.base, &topic.data, out, sizeof(*out), max_age_ms);
}

static void publish_sample(int a, float b)
{
    const Sample s = { a, b };
    topic_publish(&topic.base, &topic.data, &s, sizeof(s));
}

static void test_claim_only_once(void)
{
    TEST_ASSERT_TRUE(topic_claim(&topic.base, "first"));
    TEST_ASSERT_FALSE(topic_claim(&topic.base, "second"));
    TEST_ASSERT_EQUAL_STRING("first", topic.base.owner);
}

static void test_never_published_reads_false(void)
{
    Sample out;
    TEST_ASSERT_FALSE(read_sample(&out, TOPIC_ANY_AGE));
}

static void test_latest_value_wins(void)
{
    Sample out;
    publish_sample(1, 1.5f);
    publish_sample(2, 2.5f);
    TEST_ASSERT_TRUE(read_sample(&out, TOPIC_ANY_AGE));
    TEST_ASSERT_EQUAL_INT(2, out.a);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, out.b);
}

/* 正好 max_age 算新，再多 1 µs 算旧 */
static void test_age_boundary(void)
{
    Sample out;
    publish_sample(7, 0.0f);
    fake_time_advance_ms(200u);
    TEST_ASSERT_TRUE(read_sample(&out, 200u));
    fake_time_set_us(1000000u + 200000u + 1u);
    TEST_ASSERT_FALSE(read_sample(&out, 200u));
    TEST_ASSERT_TRUE(read_sample(&out, TOPIC_ANY_AGE));
}

static void test_failed_read_leaves_out_untouched(void)
{
    Sample out = { 42, 4.2f };
    publish_sample(1, 1.0f);
    fake_time_advance_ms(500u);
    TEST_ASSERT_FALSE(read_sample(&out, 100u));
    TEST_ASSERT_EQUAL_INT(42, out.a);
}

/* 读取方取完时刻后恰好有新的发布：时间戳比读取时刻还新，按“刚发布”处理 */
static void test_stamp_newer_than_reader_is_fresh(void)
{
    Sample out;
    fake_time_set_us(2000000u);
    publish_sample(3, 0.0f);
    fake_time_set_us(1999000u);
    TEST_ASSERT_TRUE(read_sample(&out, 0u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_claim_only_once);
    RUN_TEST(test_never_published_reads_false);
    RUN_TEST(test_latest_value_wins);
    RUN_TEST(test_age_boundary);
    RUN_TEST(test_failed_read_leaves_out_untouched);
    RUN_TEST(test_stamp_newer_than_reader_is_fresh);
    return UNITY_END();
}
