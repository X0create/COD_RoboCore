/** @file test_sysid_timing.c
 * @brief 实际插件的诊断测点、空队列和忙重试；另导出普通/诊断帧作逐字节比较。
 */
#include "01_applic/modules/motor_bench/telemetry/sysid/sysid.h"
#include "05_platform/trace/trace.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

static float sample;
static bool busy;
static uint32_t trace_count, sent_count;
static RmTraceEventType trace_types[64];
static uint32_t trace_data[64];
static uint8_t sent[4][SYSID_FRAME_DATA_MAX + 6u], attempted[SYSID_FRAME_DATA_MAX + 6u];
static uint16_t sent_length[4], attempted_length;
static const SysIdChannel channels[] = { SYSID_F32("speed", "rad/s", &sample) };

void rm_trace_emit(RmTraceEventType event, uint32_t data)
{
    TEST_ASSERT_LESS_THAN_UINT32(64u, trace_count);
    trace_types[trace_count] = event;
    trace_data[trace_count++] = data;
}

static bool send_frame(const uint8_t *frame, uint16_t length)
{
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(attempted), length);
    memcpy(attempted, frame, length);
    attempted_length = length;
    if (busy)
    {
        return false;
    }
    TEST_ASSERT_LESS_THAN_UINT32(4u, sent_count);
    memcpy(sent[sent_count], frame, length);
    sent_length[sent_count++] = length;
    return true;
}

static const SysIdConfig config = { .source = "timing test",
                                    .channels = channels,
                                    .channel_count = 1u,
                                    .period_s = 0.001f,
                                    .clock_hz = 1000000u,
                                    .send = send_frame };

void setUp(void)
{
    sample = 1.5f;
    busy = false;
    trace_count = sent_count = 0u;
    TEST_ASSERT_TRUE(sysid_init(&config));
}

void tearDown(void)
{
}

static void assert_empty_peek(void)
{
#if defined(RM_MOTOR_BENCH_TIMING)
    TEST_ASSERT_EQUAL_UINT32(2u, trace_count);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_LOG_PEEK_BEGIN, trace_types[0]);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_LOG_PEEK_END, trace_types[1]);
    TEST_ASSERT_EQUAL_UINT32(0u, trace_data[1]);
#else
    TEST_ASSERT_EQUAL_UINT32(0u, trace_count);
#endif
}

static void assert_frame_phases(uint8_t type, uint16_t length)
{
#if defined(RM_MOTOR_BENCH_TIMING)
    static const RmTraceEventType expected[] = { RM_TRACE_LOG_PEEK_BEGIN, RM_TRACE_LOG_PEEK_END,
                                                 RM_TRACE_ENCODE_BEGIN,   RM_TRACE_CRC_BEGIN,
                                                 RM_TRACE_CRC_END,        RM_TRACE_ENCODE_END,
                                                 RM_TRACE_LOG_PEEK_BEGIN, RM_TRACE_LOG_PEEK_END };
    TEST_ASSERT_EQUAL_UINT32(8u, trace_count);
    TEST_ASSERT_EQUAL_MEMORY(expected, trace_types, sizeof(expected));
    TEST_ASSERT_EQUAL_UINT32(((uint32_t)type << 16) | length, trace_data[1]);
    TEST_ASSERT_EQUAL_UINT32(trace_data[1], trace_data[2]);
    TEST_ASSERT_EQUAL_UINT32(length, trace_data[3]);
    TEST_ASSERT_EQUAL_UINT32(
        (uint32_t)attempted[length + 4u] | ((uint32_t)attempted[length + 5u] << 8), trace_data[4]);
    TEST_ASSERT_EQUAL_UINT32(length + 6u, trace_data[5]);
    TEST_ASSERT_EQUAL_UINT32(0u, trace_data[7]);
#else
    (void)type;
    (void)length;
    TEST_ASSERT_EQUAL_UINT32(0u, trace_count);
#endif
}

static void test_schema_and_record_wire_data(void)
{
    sysid_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, sent_count);
    TEST_ASSERT_EQUAL_HEX8(0x10u, sent[0][3]);
    assert_frame_phases(0x10u, (uint16_t)(sent_length[0] - 6u));
    trace_count = 0u;
    sysid_record(1234u);
    sysid_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, sent_count);
    TEST_ASSERT_EQUAL_UINT16(42u, sent_length[1]);
    TEST_ASSERT_EQUAL_HEX8(0x11u, sent[1][3]);
    static const uint8_t timestamp[] = { 0xd2u, 0x04u, 0u, 0u };
    static const uint8_t value[] = { 0u, 0u, 0xc0u, 0x3fu }; /* 1.5f，线格式固定为 LE IEEE754。 */
    TEST_ASSERT_EQUAL_MEMORY(timestamp, &sent[1][12], sizeof(timestamp));
    TEST_ASSERT_EQUAL_MEMORY(value, &sent[1][36], sizeof(value));
    assert_frame_phases(0x11u, 36u);
}

static void test_empty_queue_does_not_encode(void)
{
    sysid_poll();
    trace_count = 0u;
    sysid_poll();
    TEST_ASSERT_EQUAL_UINT32(1u, sent_count);
    assert_empty_peek();
}

static void test_busy_retry_keeps_encoded_bytes(void)
{
    sysid_poll();
    trace_count = 0u;
    sysid_record(1234u);
    busy = true;
    sysid_poll();
    uint8_t pending[42];
    TEST_ASSERT_EQUAL_UINT16(sizeof(pending), attempted_length);
    memcpy(pending, attempted, sizeof(pending));
#if defined(RM_MOTOR_BENCH_TIMING)
    TEST_ASSERT_EQUAL_UINT32(6u, trace_count);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ENCODE_END, trace_types[5]);
#endif
    trace_count = 0u;
    busy = false;
    sysid_poll();
    TEST_ASSERT_EQUAL_UINT32(2u, sent_count);
    TEST_ASSERT_EQUAL_MEMORY(pending, sent[1], sizeof(pending));
    assert_empty_peek(); /* 重试不重新 peek/编码，仅发送成功后检查队列是否已空。 */
}

static int dump_frames(const char *path)
{
    setUp();
    sysid_poll();
    sysid_record(1234u);
    sysid_poll();
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
    {
        return 1;
    }
    bool complete = true;
    for (uint32_t i = 0u; i < sent_count; ++i)
    {
        complete = fwrite(sent[i], 1u, sent_length[i], stream) == sent_length[i] && complete;
    }
    return fclose(stream) == 0 && complete ? 0 : 1;
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_schema_and_record_wire_data);
    RUN_TEST(test_empty_queue_does_not_encode);
    RUN_TEST(test_busy_retry_keeps_encoded_bytes);
    const int result = UNITY_END();
    if (result == 0 && argc == 3 && strcmp(argv[1], "--dump") == 0)
    {
        return dump_frames(argv[2]);
    }
    return result;
}
