/**
 * @file    test_chassis.c
 * @brief   底盘子系统的单元测试（全向轮为主）：不支持力矩的电机被拒、全车停不写指令并对齐实测速度、
 *          目标按斜坡变化、各轮力矩方向符合逆解、输出限幅比例、一个轮子离线时受控减速且不给它写指令；
 *          麦轮按麦轮逆解驱动；舵轮转向电机转向目标朝向、转向电机离线同样受控减速
 */
#include "01_applic/modules/chassis/chassis.h"

#include "03_algorithm/math/math_const.h"

#include <math.h>
#include <string.h>

#include "02_devices/motor/motor.h"
#include "05_platform/time/time.h"
#include "tests/host/fakes/fake_can.h"
#include "tests/host/fakes/fake_gpio.h"
#include "tests/host/fakes/fake_time.h"
#include "unity.h"

#define DT_S CHASSIS_PERIOD_S

#define DRIVE_PID                                                                                  \
    {                                                                                              \
        .kp = 0.5f, .output_limit = 3.0f                                                           \
    }

static const ChassisConfig cfg = {
    .type = CHASSIS_OMNI,
    .omni = { .wheel_radius_m = 0.08f, .center_dist_m = 0.25f, .first_wheel_rad = RM_PI / 4.0f },
    .drive_speed_pid = DRIVE_PID,
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
};

static const ChassisConfig mecanum_cfg = {
    .type = CHASSIS_MECANUM,
    .mecanum = { .wheel_radius_m = 0.076f, .half_wheelbase_m = 0.2f, .half_track_m = 0.22f },
    .drive_speed_pid = DRIVE_PID,
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
};

static const ChassisConfig steer_cfg = {
    .type = CHASSIS_STEER,
    .steer = { .kinematics = { .wheel_radius_m = 0.06f,
                               .half_wheelbase_m = 0.2f,
                               .half_track_m = 0.2f },
               .angle_pid = { .kp = 20.0f, .output_limit = 30.0f },
               .speed_pid = { .kp = 0.1f, .output_limit = 1.0f } },
    .drive_speed_pid = DRIVE_PID,
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
};

static const ChassisConfig half_steer_cfg = {
    .type = CHASSIS_HALF_STEER,
    .steer = { .angle_pid = { .kp = 20.0f, .output_limit = 30.0f },
               .speed_pid = { .kp = 0.1f, .output_limit = 1.0f } },
    .half_steer = { .steer_diagonal = HALF_STEER_LF_RB,
                    .steer_radius_m = 0.06f,
                    .omni_radius_m = 0.076f,
                    .half_wheelbase_m = 0.2f,
                    .half_track_m = 0.15f },
    .drive_speed_pid = DRIVE_PID,
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
};

/* 电机保存配置指针，所以配置放在静态存储里；每个测试用新的 Motor（看门狗每个实例只能登记一次） */
static MotorConfig wheel_cfg[CHASSIS_WHEELS];
static MotorConfig steer_motor_cfg[CHASSIS_WHEELS];
static Motor pool[128];
static unsigned pool_used;
static MotorGroup group;

/* 相当于 comm_rx_task.c：把收到的一帧交给电机组（motor_group_receive），有电机认领就返回 true */
static bool deliver(CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    CanFrame frame = { .id = id,
                       .len = len,
                       .stamp_us = rm_time_now_us() }; /* 中断里记下的接收时刻 */
    memcpy(frame.data, data, len);
    return motor_group_receive(&group, bus, &frame);
}
static Motor *wheel[CHASSIS_WHEELS];
static Motor *steer[CHASSIS_WHEELS];
static Chassis chassis;

void setUp(void)
{
    fake_can_reset();
    fake_gpio_reset();
    fake_time_set_us(1000000u);
    group = (MotorGroup){ 0 };
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        wheel_cfg[i] = (MotorConfig){ .name = "wheel",
                                      .type = MOTOR_M3508,
                                      .can_bus = CAN_BUS_1,
                                      .id = (uint8_t)(i + 1u),
                                      .direction = 1,
                                      .gear_ratio = DJI_M3508_GEAR_RATIO,
                                      .stop_action = STOP_ACTION_ZERO_TORQUE };
        wheel[i] = &pool[pool_used++];
        const Motor *conflict;
        TEST_ASSERT_TRUE(motor_init(wheel[i], &wheel_cfg[i], &group, &conflict));
    }
    TEST_ASSERT_TRUE(chassis_init(&chassis, &cfg, wheel, NULL));
}

void tearDown(void)
{
}

/* 舵轮用：再加 4 个 M2006 作转向电机（ID 5–8，反馈 0x205–0x208），上电角度为 0 即朝前 */
static void add_steer_motors(void)
{
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        steer_motor_cfg[i] = (MotorConfig){ .name = "steer",
                                            .type = MOTOR_M2006,
                                            .can_bus = CAN_BUS_1,
                                            .id = (uint8_t)(i + 5u),
                                            .direction = 1,
                                            .gear_ratio = DJI_M2006_GEAR_RATIO,
                                            .stop_action = STOP_ACTION_ZERO_TORQUE };
        steer[i] = &pool[pool_used++];
        const Motor *conflict;
        TEST_ASSERT_TRUE(motor_init(steer[i], &steer_motor_cfg[i], &group, &conflict));
    }
}

/* 送一帧反馈：电调 ID 为 id、输出轴转速 speed_rad_s（换算成转子 rpm 填进帧里） */
static void feed_id(unsigned id, float gear, float speed_rad_s)
{
    const float rpm = speed_rad_s * gear * 60.0f / (2.0f * RM_PI);
    const int16_t raw = (int16_t)lroundf(rpm);
    const uint8_t d[8] = { 0, 0, (uint8_t)((uint16_t)raw >> 8), (uint8_t)raw, 0, 0, 0, 0 };
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x200u + id, d, 8));
}

static void feed(unsigned i, float speed_rad_s)
{
    feed_id(i + 1u, DJI_M3508_GEAR_RATIO, speed_rad_s);
}

static void feed_all(float speed_rad_s)
{
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        feed(i, speed_rad_s);
    }
}

static void feed_steer_all(void)
{
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        feed_id(i + 5u, DJI_M2006_GEAR_RATIO, 0.0f);
    }
}

static MotorConfig gm6020_cfg(void)
{
    return (MotorConfig){ .name = "gm6020",
                          .type = MOTOR_GM6020,
                          .can_bus = CAN_BUS_2,
                          .id = 1u,
                          .direction = 1,
                          .gear_ratio = 1.0f,
                          .stop_action = STOP_ACTION_ZERO_TORQUE };
}

/* control_task 一个周期的三步：能动时 */
static void run_step(Chassis *c, const ChassisVel *cmd, float output_scale)
{
    chassis_measure_update(c);
    chassis_target_update(c, cmd, false);
    chassis_output_update(c, output_scale);
}

/* control_task 一个周期的三步：全车停时 */
static void run_stop(Chassis *c)
{
    chassis_measure_update(c);
    chassis_target_update(c, NULL, true);
    chassis_output_update(c, 1.0f);
}

static void test_rejects_motor_without_torque_command(void)
{
    static MotorConfig gm;
    gm = gm6020_cfg();
    Motor *m = &pool[pool_used++];
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(m, &gm, &group, &conflict));
    Motor *const mixed[CHASSIS_WHEELS] = { wheel[0], wheel[1], m, wheel[3] };
    Chassis c;
    TEST_ASSERT_FALSE(chassis_init(&c, &cfg, mixed, NULL));
}

/* 全车停：不写任何指令（由发送出口改写成零力矩）；斜坡起点对齐实测速度（原地转） */
static void test_stop_all_writes_nothing_and_aligns_to_measured(void)
{
    feed_all(5.0f);
    run_stop(&chassis);

    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        TEST_ASSERT_FALSE(wheel[i]->torque_set);
    }
    /* 四轮同速 5 rad/s = 原地逆时针转：wz = 5 × 0.08 / 0.25（反馈经过 rpm 取整，留一点余量） */
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, chassis.target.velocity.vx_m_s);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, chassis.target.velocity.vy_m_s);
    TEST_ASSERT_FLOAT_WITHIN(5e-3f, 1.6f, chassis.target.velocity.wz_rad_s);
}

/* 解锁后目标按斜坡上升；向前走时左侧两轮负力矩、右侧两轮正力矩 */
static void test_ramps_and_drives_wheels_by_inverse_kinematics(void)
{
    feed_all(0.0f);
    run_stop(&chassis); /* 对齐到静止 */

    const ChassisVel target = { .vx_m_s = 1.0f };
    run_step(&chassis, &target, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.0f * DT_S, chassis.target.velocity.vx_m_s);
    for (int k = 1; k < 100; k++)
    {
        feed_all(0.0f);
        run_step(&chassis, &target, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.2f, chassis.target.velocity.vx_m_s);

    TEST_ASSERT_TRUE(wheel[0]->torque_set);
    TEST_ASSERT_TRUE(wheel[0]->torque_cmd_nm < 0.0f);
    TEST_ASSERT_TRUE(wheel[1]->torque_cmd_nm < 0.0f);
    TEST_ASSERT_TRUE(wheel[2]->torque_cmd_nm > 0.0f);
    TEST_ASSERT_TRUE(wheel[3]->torque_cmd_nm > 0.0f);
}

/* 输出限幅比例：解锁瞬间为 0，力矩也为 0 */
static void test_output_scale_limits_torque(void)
{
    feed_all(0.0f);
    run_stop(&chassis);
    const ChassisVel target = { .wz_rad_s = 4.0f };
    for (int k = 0; k < 500; k++)
    {
        feed_all(0.0f);
        run_step(&chassis, &target, 1.0f);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, wheel[0]->torque_cmd_nm); /* 已到 PID 输出限幅 */

    feed_all(0.0f);
    run_step(&chassis, &target, 0.25f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, wheel[0]->torque_cmd_nm);
    feed_all(0.0f);
    run_step(&chassis, &target, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, wheel[0]->torque_cmd_nm);
}

/* 降级：轮 2 离线后其余轮子继续，目标朝一半速度、按一半的加速度走；轮 2 不写指令 */
static void test_wheel_offline_degrades_to_half_speed(void)
{
    feed_all(0.0f);
    run_stop(&chassis);
    const ChassisVel target = { .vx_m_s = 1.0f };
    for (int k = 0; k < 500; k++)
    {
        feed_all(0.0f);
        run_step(&chassis, &target, 1.0f);
        motor_group_send(&group);
        fake_time_advance_ms(1u);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, chassis.target.velocity.vx_m_s);

    /* 轮 2 不再有反馈，超过离线时间 */
    for (int k = 0; k <= (int)MOTOR_TIMEOUT_MS; k++)
    {
        feed(0, 0.0f);
        feed(1, 0.0f);
        feed(3, 0.0f);
        fake_time_advance_ms(1u);
    }
    const float before = chassis.target.velocity.vx_m_s;
    run_step(&chassis, &target, 1.0f);
    TEST_ASSERT_FALSE(chassis.measure.all_online);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, before - 2.0f * CHASSIS_DEGRADED_SCALE * DT_S,
                             chassis.target.velocity.vx_m_s);
    TEST_ASSERT_FALSE(wheel[2]->torque_set);
    TEST_ASSERT_TRUE(wheel[0]->torque_set);
    TEST_ASSERT_TRUE(wheel[3]->torque_set);

    for (int k = 0; k < 1000; k++) /* 1 s 后稳定在一半速度，不会停下 */
    {
        feed(0, 0.0f);
        feed(1, 0.0f);
        feed(3, 0.0f);
        run_step(&chassis, &target, 1.0f);
        motor_group_send(&group);
        fake_time_advance_ms(1u);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CHASSIS_DEGRADED_SCALE * 1.0f, chassis.target.velocity.vx_m_s);
    feed(0, 0.0f);
    feed(1, 0.0f);
    feed(3, 0.0f);
    run_step(&chassis, &target, 1.0f); /* 再算一拍、不发送：发送会清掉本周期的指令标记 */
    TEST_ASSERT_FALSE(wheel[2]->torque_set);
    TEST_ASSERT_TRUE(wheel[1]->torque_set);
}

/* 麦轮：向左平移时左前、右后轮负力矩，左后、右前轮正力矩 */
static void test_mecanum_drives_by_mecanum_inverse(void)
{
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &mecanum_cfg, wheel, NULL));
    feed_all(0.0f);
    run_stop(&c);
    const ChassisVel target = { .vy_m_s = 1.0f };
    for (int k = 0; k < 50; k++)
    {
        feed_all(0.0f);
        run_step(&c, &target, 1.0f);
    }
    TEST_ASSERT_TRUE(wheel[0]->torque_cmd_nm < 0.0f);
    TEST_ASSERT_TRUE(wheel[1]->torque_cmd_nm > 0.0f);
    TEST_ASSERT_TRUE(wheel[2]->torque_cmd_nm < 0.0f);
    TEST_ASSERT_TRUE(wheel[3]->torque_cmd_nm > 0.0f);
}

/* 舵轮：向左前方平移，斜坡过程中方向不变，各轮目标朝向一直是 atan2(1, 0.5)，
 * 转向电机往正方向出力；驱动轮正力矩 */
static void test_steer_turns_wheels_toward_target(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_cfg, wheel, steer));
    feed_all(0.0f);
    feed_steer_all();
    run_stop(&c);
    const ChassisVel target = { .vx_m_s = 0.5f, .vy_m_s = 1.0f };
    for (int k = 0; k < 50; k++)
    {
        feed_all(0.0f);
        feed_steer_all();
        run_step(&c, &target, 1.0f);
    }
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(1.0f, 0.5f), c.target.heading_rad[i]);
        TEST_ASSERT_TRUE(steer[i]->torque_set);
        TEST_ASSERT_TRUE(steer[i]->torque_cmd_nm > 0.0f);
        TEST_ASSERT_TRUE(wheel[i]->torque_cmd_nm > 0.0f);
    }
}

/* 半舵半全向：只有舵轮（左前、右后）有转向电机，全向轮位置传 NULL；只给这两个转向电机反馈也算全部在线，
 * 平移时两个舵轮朝向目标方向，只有它们的转向电机收到指令 */
static void test_half_steer_turns_only_steer_wheels(void)
{
    add_steer_motors();
    Motor *const half[CHASSIS_WHEELS] = { steer[0], NULL, steer[2], NULL };
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &half_steer_cfg, wheel, half));
    const ChassisVel target = { .vx_m_s = 0.5f, .vy_m_s = 1.0f };
    for (int k = 0; k < 50; k++)
    {
        feed_all(0.0f);
        feed_id(5u, DJI_M2006_GEAR_RATIO, 0.0f);
        feed_id(7u, DJI_M2006_GEAR_RATIO, 0.0f);
        run_step(&c, &target, 1.0f);
    }
    TEST_ASSERT_TRUE(c.measure.all_online);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(1.0f, 0.5f), c.target.heading_rad[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(1.0f, 0.5f), c.target.heading_rad[2]);
    TEST_ASSERT_TRUE(steer[0]->torque_set && steer[0]->torque_cmd_nm > 0.0f);
    TEST_ASSERT_TRUE(steer[2]->torque_set && steer[2]->torque_cmd_nm > 0.0f);
    TEST_ASSERT_FALSE(steer[1]->torque_set);
    TEST_ASSERT_FALSE(steer[3]->torque_set);
}

/* 舵轮：转向电机不支持力矩指令（GM6020 目前只有反馈）时拒绝 */
static void test_steer_rejects_motor_without_torque_command(void)
{
    static MotorConfig gm;
    gm = gm6020_cfg();
    add_steer_motors();
    Motor *m = &pool[pool_used++];
    const Motor *conflict;
    TEST_ASSERT_TRUE(motor_init(m, &gm, &group, &conflict));
    Motor *const mixed[CHASSIS_WHEELS] = { steer[0], m, steer[2], steer[3] };
    Chassis c;
    TEST_ASSERT_FALSE(chassis_init(&c, &steer_cfg, wheel, mixed));
}

/* 舵轮：转向电机离线 → 这个轮子朝向不受控，驱动和转向都不写指令；其余轮子降级继续 */
static void test_steer_motor_offline_drops_that_wheel(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_cfg, wheel, steer));
    feed_all(0.0f);
    feed_steer_all();
    run_stop(&c);
    const ChassisVel target = { .vx_m_s = 1.0f };
    for (int k = 0; k < 100; k++)
    {
        feed_all(0.0f);
        feed_steer_all();
        run_step(&c, &target, 1.0f);
        motor_group_send(&group);
        fake_time_advance_ms(1u);
    }
    for (int k = 0; k <= (int)MOTOR_TIMEOUT_MS; k++)
    {
        feed_all(0.0f);
        feed_id(5u, DJI_M2006_GEAR_RATIO, 0.0f);
        feed_id(6u, DJI_M2006_GEAR_RATIO, 0.0f);
        feed_id(7u, DJI_M2006_GEAR_RATIO, 0.0f); /* 轮 3 的转向电机（ID 8）不再有反馈 */
        fake_time_advance_ms(1u);
    }
    const float before = c.target.velocity.vx_m_s;
    run_step(&c, &target, 1.0f);
    TEST_ASSERT_FALSE(c.measure.all_online);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, before + 2.0f * CHASSIS_DEGRADED_SCALE * DT_S,
                             c.target.velocity.vx_m_s); /* 还没到一半速度：按一半的加速度继续加速 */
    TEST_ASSERT_FALSE(steer[3]->torque_set);
    TEST_ASSERT_FALSE(wheel[3]->torque_set); /* 驱动电机在线，但朝向不受控，不驱动 */
    TEST_ASSERT_TRUE(steer[0]->torque_set);
    TEST_ASSERT_TRUE(wheel[0]->torque_set);
}

/* ---- 舵轮找零 ---- */

/* 轮 0 的转向电机接零点开关 1（低电平有效），其余轮子不接开关 */
static const ChassisConfig steer_zero_cfg = {
    .type = CHASSIS_STEER,
    .steer = { .kinematics = { .wheel_radius_m = 0.06f,
                               .half_wheelbase_m = 0.2f,
                               .half_track_m = 0.2f },
               .angle_pid = { .kp = 20.0f, .output_limit = 30.0f },
               .speed_pid = { .kp = 0.1f, .output_limit = 1.0f },
               .zero_switch = { GPIO_IN_ZERO_SWITCH_1 },
               .zero_rad = { 0.05f },
               .zero_switch_active_low = true,
               .zero_search_rad_s = 2.0f,
               .zero_search_timeout_s = 3.0f },
    .drive_speed_pid = DRIVE_PID,
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
};

#define SWITCH_CENTER_RAD     1.0f
#define SWITCH_HALF_WIDTH_RAD 0.1f

/* 转向电机 i 的反馈：输出轴角度换成转子编码器计数（M2006 减速比 36），第一帧为 0 */
static void feed_steer_angle(unsigned i, float angle_rad, float speed_rad_s)
{
    const float rotor_rad = angle_rad * DJI_M2006_GEAR_RATIO;
    const long counts = lroundf(rotor_rad / (2.0f * RM_PI) * (float)DJI_ENCODER_COUNTS);
    const uint16_t enc =
        (uint16_t)(((counts % DJI_ENCODER_COUNTS) + DJI_ENCODER_COUNTS) % DJI_ENCODER_COUNTS);
    const float rpm = speed_rad_s * DJI_M2006_GEAR_RATIO * 60.0f / (2.0f * RM_PI);
    const int16_t raw = (int16_t)lroundf(rpm);
    const uint8_t d[8] = {
        (uint8_t)(enc >> 8), (uint8_t)enc, (uint8_t)((uint16_t)raw >> 8), (uint8_t)raw, 0, 0, 0, 0
    };
    TEST_ASSERT_TRUE(deliver(CAN_BUS_1, 0x200u + 5u + i, d, 8));
}

/*
 * 模拟一拍：轮 0 的转向电机按找零状态转（正转 +2 rad/s、反转 −2 rad/s），开关在 1.0 ± 0.1 rad 内触发。
 * 返回模拟出的角度
 */
static float zero_sim_step(Chassis *c, float angle_rad, bool switch_present)
{
    const float v = c->zero[0].state == STEER_ZERO_FORWARD
                        ? 2.0f
                        : (c->zero[0].state == STEER_ZERO_REVERSE ? -2.0f : 0.0f);
    angle_rad += v * DT_S;
    const bool active =
        switch_present && fabsf(angle_rad - SWITCH_CENTER_RAD) <= SWITCH_HALF_WIDTH_RAD;
    fake_gpio_set(GPIO_IN_ZERO_SWITCH_1, !active); /* 低电平有效 */
    feed_all(0.0f);
    feed_steer_angle(0u, angle_rad, v);
    for (unsigned i = 1u; i < CHASSIS_WHEELS; i++)
    {
        feed_steer_angle(i, 0.0f, 0.0f);
    }
    const ChassisVel zero = { 0 };
    run_step(c, &zero, 1.0f);
    fake_time_advance_ms(1u);
    return angle_rad;
}

/* 正转、反转各经过开关一次，开关中心找对（轮询滞后正反抵消）；找到前这个轮子不能用、驱动不出力 */
static void test_steer_zero_finds_switch_center(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_zero_cfg, wheel, steer));
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_FORWARD, c.zero[0].state);
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_DONE, c.zero[1].state); /* 没接开关：零点上电即确定 */

    float angle = 0.0f;
    angle = zero_sim_step(&c, angle, true);
    TEST_ASSERT_FALSE(c.measure.wheel_ok[0]);
    TEST_ASSERT_FALSE(wheel[0]->torque_set);
    TEST_ASSERT_TRUE(steer[0]->torque_set);
    for (int k = 0; k < 2000 && c.zero[0].state != STEER_ZERO_DONE; k++)
    {
        angle = zero_sim_step(&c, angle, true);
    }
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_DONE, c.zero[0].state);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, SWITCH_CENTER_RAD, c.zero[0].center_rad);

    angle = zero_sim_step(&c, angle, true);
    TEST_ASSERT_TRUE(c.measure.wheel_ok[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, angle - SWITCH_CENTER_RAD - 0.05f, c.measure.heading_rad[0]);
}

/* 找不到开关：超时后判失败，这个轮子不能用、转向电机也不再出力 */
static void test_steer_zero_timeout_fails(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_zero_cfg, wheel, steer));
    float angle = 0.0f;
    for (int k = 0; k < 3100; k++)
    {
        angle = zero_sim_step(&c, angle, false);
    }
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_FAILED, c.zero[0].state);
    motor_group_send(&group); /* 清掉前面找零时写过的指令，再看失败后的一拍 */
    angle = zero_sim_step(&c, angle, false);
    TEST_ASSERT_FALSE(c.measure.wheel_ok[0]);
    TEST_ASSERT_FALSE(steer[0]->torque_set);
    TEST_ASSERT_TRUE(c.measure.wheel_ok[1]); /* 其余轮子照常（降级） */
}

/* 全车停时找零暂停、不计时；转向电机离线再上线后重新找零 */
static void test_steer_zero_pauses_and_restarts_after_offline(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_zero_cfg, wheel, steer));
    for (int k = 0; k < 100; k++)
    {
        feed_all(0.0f);
        feed_steer_all();
        run_stop(&c);
        fake_time_advance_ms(1u);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, c.zero[0].elapsed_s);

    float angle = 0.0f;
    for (int k = 0; k < 2000 && c.zero[0].state != STEER_ZERO_DONE; k++)
    {
        angle = zero_sim_step(&c, angle, true);
    }
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_DONE, c.zero[0].state);

    for (int k = 0; k <= (int)MOTOR_TIMEOUT_MS; k++) /* 轮 0 的转向电机不再有反馈 */
    {
        fake_time_advance_ms(1u);
    }
    feed_all(0.0f);
    run_stop(&c);
    TEST_ASSERT_EQUAL_INT(STEER_ZERO_FORWARD, c.zero[0].state);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_motor_without_torque_command);
    RUN_TEST(test_stop_all_writes_nothing_and_aligns_to_measured);
    RUN_TEST(test_ramps_and_drives_wheels_by_inverse_kinematics);
    RUN_TEST(test_output_scale_limits_torque);
    RUN_TEST(test_wheel_offline_degrades_to_half_speed);
    RUN_TEST(test_mecanum_drives_by_mecanum_inverse);
    RUN_TEST(test_steer_turns_wheels_toward_target);
    RUN_TEST(test_steer_rejects_motor_without_torque_command);
    RUN_TEST(test_steer_motor_offline_drops_that_wheel);
    RUN_TEST(test_half_steer_turns_only_steer_wheels);
    RUN_TEST(test_steer_zero_finds_switch_center);
    RUN_TEST(test_steer_zero_timeout_fails);
    RUN_TEST(test_steer_zero_pauses_and_restarts_after_offline);
    return UNITY_END();
}
