/**
 * @file    test_safety_gate.c
 * @brief   安全门的单元测试：启动前不能解锁、必须先拨下再拨上、急停、遥控丢失、重新解锁、输出斜坡
 */
#include "safety_gate.h"

#include "unity.h"

static SafetyGate gate;
static uint64_t now_us;

void setUp(void)
{
    safety_gate_init(&gate, 1u); /* 右拨杆 sw[1] */
    now_us = 1000000u;
}

void tearDown(void)
{
}

static RcState rc_with(RcSwitch arm, RcSwitch other)
{
    RcState rc = { 0 };
    rc.sw[1] = arm;
    rc.sw[0] = other;
    return rc;
}

/* 走一步：arm 拨杆位置；online 为 false 表示遥控丢失 */
static SafetyDecision step(bool online, RcSwitch arm)
{
    const RcState rc = rc_with(arm, RC_SW_MID);
    now_us += 1000u;
    return safety_gate_update(&gate, online ? &rc : NULL, now_us);
}

static void arm(void)
{
    step(true, RC_SW_DOWN);
    TEST_ASSERT_FALSE(step(true, RC_SW_UP).stop_all);
}

static void test_cannot_arm_before_system_ready(void)
{
    TEST_ASSERT_TRUE(step(true, RC_SW_DOWN).stop_all);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
    TEST_ASSERT_EQUAL_INT(ROBOT_MODE_INIT, gate.mode);
}

/* 上电时拨杆已经在上方：不会直接开动，必须先拨下 */
static void test_needs_down_then_up(void)
{
    safety_gate_set_system_ready(&gate);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
    TEST_ASSERT_EQUAL_INT(ROBOT_MODE_SAFE, gate.mode);
    TEST_ASSERT_TRUE(step(true, RC_SW_DOWN).stop_all);
    const SafetyDecision d = step(true, RC_SW_MID); /* 拨到中也算解锁 */
    TEST_ASSERT_FALSE(d.stop_all);
    TEST_ASSERT_TRUE(d.entered_manual);
    TEST_ASSERT_FALSE(step(true, RC_SW_UP).entered_manual); /* 进入事件只有一次 */
}

static void test_estop_then_rearm(void)
{
    safety_gate_set_system_ready(&gate);
    step(true, RC_SW_UP);
    arm();
    TEST_ASSERT_TRUE(step(true, RC_SW_DOWN).stop_all); /* 急停 */
    TEST_ASSERT_EQUAL_INT(ROBOT_MODE_SAFE, gate.mode);
    TEST_ASSERT_TRUE(step(true, RC_SW_DOWN).stop_all);
    TEST_ASSERT_FALSE(step(true, RC_SW_UP).stop_all); /* 拨下再拨上：重新解锁 */
}

/* 遥控丢失后恢复：拨杆仍在上方也不动，要重新拨一次 */
static void test_rc_lost_requires_rearm(void)
{
    safety_gate_set_system_ready(&gate);
    step(true, RC_SW_UP);
    arm();
    TEST_ASSERT_TRUE(step(false, RC_SW_UP).stop_all);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
    step(true, RC_SW_DOWN);
    TEST_ASSERT_FALSE(step(true, RC_SW_UP).stop_all);
}

/* Safe 里看到过“下”，但随后遥控丢失：恢复后不能凭旧记录解锁 */
static void test_rc_lost_in_safe_forgets_down(void)
{
    safety_gate_set_system_ready(&gate);
    step(true, RC_SW_UP);
    step(true, RC_SW_DOWN);
    step(false, RC_SW_DOWN);
    TEST_ASSERT_TRUE(step(true, RC_SW_UP).stop_all);
}

/* 只看配置的拨杆：另一个拨杆拨下不算急停 */
static void test_only_configured_switch_counts(void)
{
    safety_gate_set_system_ready(&gate);
    step(true, RC_SW_UP);
    arm();
    const RcState rc = rc_with(RC_SW_UP, RC_SW_DOWN);
    now_us += 1000u;
    TEST_ASSERT_FALSE(safety_gate_update(&gate, &rc, now_us).stop_all);
}

/* 进入 Manual 后 300 ms 内输出比例从 0 线性升到 1 */
static void test_output_ramp(void)
{
    safety_gate_set_system_ready(&gate);
    step(true, RC_SW_UP);
    step(true, RC_SW_DOWN);
    step(true, RC_SW_UP);
    const uint64_t t0 = gate.manual_since_us;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, safety_gate_output_scale(&gate, t0));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, safety_gate_output_scale(&gate, t0 + 150000u));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, safety_gate_output_scale(&gate, t0 + 300000u));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, safety_gate_output_scale(&gate, t0 + 5000000u));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_cannot_arm_before_system_ready);
    RUN_TEST(test_needs_down_then_up);
    RUN_TEST(test_estop_then_rearm);
    RUN_TEST(test_rc_lost_requires_rearm);
    RUN_TEST(test_rc_lost_in_safe_forgets_down);
    RUN_TEST(test_only_configured_switch_counts);
    RUN_TEST(test_output_ramp);
    return UNITY_END();
}
