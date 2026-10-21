#include "05_platform/trace/trace.h"
#include "unity.h"
#include <stdio.h>
#include <string.h>

static uint32_t cycles, context, mask, locks, barriers;
static RmTrace frozen;

uint32_t rm_trace_hw_lock(void)
{
    const uint32_t saved = mask;
    mask = 1u;
    ++locks;
    return saved;
}
void rm_trace_hw_unlock(uint32_t saved)
{
    TEST_ASSERT_EQUAL_UINT32(1u, mask);
    mask = saved;
}
uint32_t rm_trace_hw_cycles(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, mask);
    return cycles++;
}
uint32_t rm_trace_hw_context(void)
{
    return context;
}
void rm_trace_hw_barrier(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, mask);
    ++barriers;
}
void rm_trace_hw_irqs(void)
{
    TEST_ASSERT_EQUAL_UINT32(1u, mask);
    rm_trace.enabled_irqs[0] |= 4u;
}
void setUp(void)
{
    cycles = context = mask = locks = barriers = 0u;
    rm_trace_reset(550000000u);
    rm_trace_bind_tasks(0x100u, 0x200u, 0x300u);
}
void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, mask);
}
static void arm(void)
{
    rm_trace.request = 1u;
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ARMED, rm_trace.state);
}
static uint32_t event_type(uint32_t index)
{
    return rm_trace.events[index].event & 0xffffu;
}
static void test_disabled_and_ready_filter(void)
{
    uint32_t side_effect = 0u;
    RM_TRACE_EVENT(RM_TRACE_SAMPLE, ++side_effect);
    RM_TRACE_ENTER(++side_effect);
    RM_TRACE_STATE(++side_effect, false);
    RM_TRACE_END();
    TEST_ASSERT_EQUAL_UINT32(0u, side_effect);
    rm_trace_emit(RM_TRACE_SAMPLE, 123u);
    TEST_ASSERT_EQUAL_UINT32(0u, rm_trace.count);
    arm();
    rm_trace_ready(0x200u);
    TEST_ASSERT_EQUAL_UINT32(2u, rm_trace.count);
    context = 15u;
    rm_trace_ready(0x100u);
    TEST_ASSERT_EQUAL_UINT32(3u, rm_trace.count);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_READY, event_type(2u));
    TEST_ASSERT_EQUAL_UINT32(15u, rm_trace.events[2].event >> 16);
}
static void test_arm_boundary_and_latched_configuration(void)
{
    rm_trace.request = 1u;
    rm_trace_control_state(0u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REJECTED, rm_trace.state);
    rm_trace.request = 1u;
    rm_trace_control_state(42u, true);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_MOTION, rm_trace.reason);
    rm_trace.threshold_us = 1u;
    rm_trace.request = 1u;
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_CONFIG, rm_trace.reason);
    rm_trace.threshold_us = RM_TRACE_SCHEMA_THRESHOLD_US;
    arm();
    rm_trace.threshold_us = RM_TRACE_THRESHOLD_US;
    rm_trace_control_enter(100000u);
    rm_trace_control_state(42u, false);
    rm_trace_control_enter(101100u);
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ARMED, rm_trace.state);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_SCHEMA_THRESHOLD_US, rm_trace.captured_threshold_us);
}
static void test_trigger_tail_and_frozen_snapshot(void)
{
    arm();
    rm_trace_control_enter(1000u);
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ARMED, rm_trace.state);
    rm_trace_control_enter(2050u);
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ARMED, rm_trace.state);
    rm_trace_control_enter(3101u);
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_TRIGGERED, rm_trace.state);
    TEST_ASSERT_EQUAL_UINT32(1051u, rm_trace.last_period_us);
    rm_trace_control_end();
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_TAIL_CYCLES, rm_trace.tail_remaining);
    for (uint32_t i = 1u; i <= RM_TRACE_TAIL_CYCLES; ++i)
    {
        rm_trace_control_enter(3101u + 1000u * i);
        rm_trace_control_state(42u, false);
        rm_trace_control_end();
        TEST_ASSERT_EQUAL_UINT32(i == RM_TRACE_TAIL_CYCLES ? RM_TRACE_FROZEN : RM_TRACE_TRIGGERED,
                                 rm_trace.state);
    }
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_PERIOD, rm_trace.reason);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_FREEZE, event_type(rm_trace.count - 1u));
    memcpy(&frozen, (const void *)&rm_trace, sizeof(frozen));
    for (uint32_t i = 0u; i < 100u; ++i)
    {
        rm_trace_emit(RM_TRACE_IRQ_IN, i);
        rm_trace_ready(0x100u);
        rm_trace_control_enter(100000u);
        rm_trace_control_state(42u, false);
        rm_trace_control_end();
    }
    TEST_ASSERT_EQUAL_MEMORY(&frozen, (const void *)&rm_trace, sizeof(frozen));
}
static void test_overwrite_commit_order_and_rearm(void)
{
    arm();
    for (uint32_t i = 0u; i < RM_TRACE_CAPACITY + 20u; ++i)
    {
        rm_trace_emit(RM_TRACE_SAMPLE, i);
    }
    TEST_ASSERT_EQUAL_UINT32(22u, rm_trace.overwritten);
    const uint32_t first = rm_trace.count - RM_TRACE_CAPACITY + 1u;
    for (uint32_t seq = first; seq <= rm_trace.count; ++seq)
    {
        const uint32_t index = (seq - 1u) & (RM_TRACE_CAPACITY - 1u);
        TEST_ASSERT_EQUAL_UINT32(seq, rm_trace.events[index].sequence);
        TEST_ASSERT_EQUAL_UINT32(seq - 3u, rm_trace.events[index].data);
    }
    TEST_ASSERT_EQUAL_UINT32(rm_trace.count, barriers);
    rm_trace.request = 2u;
    rm_trace_control_state(42u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_MANUAL, rm_trace.reason);
    arm();
    TEST_ASSERT_EQUAL_UINT32(2u, rm_trace.capture_id);
    TEST_ASSERT_EQUAL_UINT32(2u, rm_trace.count);
    TEST_ASSERT_EQUAL_UINT32(0u, rm_trace.overwritten);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_ARM, event_type(0u));
}
static void test_wrap_context_and_preserve_interrupt_mask(void)
{
    arm();
    rm_trace_control_enter(UINT32_MAX - 500u);
    rm_trace_control_enter(499u);
    TEST_ASSERT_EQUAL_UINT32(1000u, rm_trace.last_period_us);
    mask = 1u;
    cycles = UINT32_MAX;
    context = 42u;
    rm_trace_emit(RM_TRACE_IRQ_IN, 7u);
    context = 43u;
    rm_trace_emit(RM_TRACE_IRQ_IN, 8u);
    rm_trace_emit(RM_TRACE_IRQ_OUT, 8u);
    context = 42u;
    rm_trace_emit(RM_TRACE_IRQ_OUT, 7u);
    TEST_ASSERT_EQUAL_UINT32(1u, mask);
    mask = 0u;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, rm_trace.events[4].cycles);
    TEST_ASSERT_EQUAL_UINT32(0u, rm_trace.events[5].cycles);
    TEST_ASSERT_EQUAL_UINT32(43u, rm_trace.events[6].event >> 16);
    TEST_ASSERT_EQUAL_UINT32(42u, rm_trace.events[7].event >> 16);
}
static void test_session_motion_and_fault_end_capture(void)
{
    arm();
    rm_trace_control_state(43u, false);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_SESSION, rm_trace.reason);
    arm();
    rm_trace_control_state(42u, true);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_MOTION, rm_trace.reason);
    arm();
    rm_trace_fault();
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_FROZEN, rm_trace.state);
    TEST_ASSERT_EQUAL_UINT32(RM_TRACE_REASON_FAULT, rm_trace.reason);
}
/* 实际 C 记录器生成二进制夹具，交给独立 Python 解码器验证 ABI / 字段 / 嵌套。 */
static int dump_fixture(const char *path)
{
    setUp();
    rm_trace_reset(1000000u);
    rm_trace_bind_tasks(0x100u, 0x200u, 0x300u);
    arm();
    rm_trace_emit(RM_TRACE_POLL_BEGIN, 0u);
    rm_trace_emit(RM_TRACE_FRAME_BEGIN, (0x10u << 16) | 472u);
    rm_trace_emit(RM_TRACE_FRAME_END, 1u);
    rm_trace_emit(RM_TRACE_POLL_END, 0u);
    context = 15u;
    cycles = 100u;
    rm_trace_emit(RM_TRACE_IRQ_IN, 0u);
    rm_trace_ready(0x100u);
    context = 42u;
    cycles = 200u;
    rm_trace_emit(RM_TRACE_IRQ_IN, 0u);
    cycles = 300u;
    rm_trace_emit(RM_TRACE_IRQ_OUT, 0u);
    context = 15u;
    cycles = 400u;
    rm_trace_emit(RM_TRACE_IRQ_OUT, 0u);
    context = 14u;
    cycles = 450u;
    rm_trace_emit(RM_TRACE_TASK_IN, 0x100u);
    context = 0u;
    cycles = 500u;
    rm_trace_control_enter(1000u);
    rm_trace_control_state(42u, false);
    rm_trace_emit(RM_TRACE_SAMPLE, 1000u);
    rm_trace_control_end();
    rm_trace.request = 2u;
    rm_trace_control_state(42u, false);
    FILE *stream = fopen(path, "wb");
    if (stream == NULL)
    {
        return 1;
    }
    const bool complete =
        fwrite((const void *)&rm_trace, 1u, sizeof(rm_trace), stream) == sizeof(rm_trace);
    const int closed = fclose(stream);
    return complete && closed == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_disabled_and_ready_filter);
    RUN_TEST(test_arm_boundary_and_latched_configuration);
    RUN_TEST(test_trigger_tail_and_frozen_snapshot);
    RUN_TEST(test_overwrite_commit_order_and_rearm);
    RUN_TEST(test_wrap_context_and_preserve_interrupt_mask);
    RUN_TEST(test_session_motion_and_fault_end_capture);
    const int result = UNITY_END();
    if (result == 0 && argc == 3 && strcmp(argv[1], "--dump") == 0)
    {
        return dump_fixture(argv[2]);
    }
    return result;
}
