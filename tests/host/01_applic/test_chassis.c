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

#include "02_devices/motor/motor_group.h"
#include "05_platform/time/time.h"
#include "fake_can.h"
#include "fake_time.h"
#include "unity.h"

#define DT_S 0.001f

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

/* 电机保存配置指针，所以配置放在静态存储里；每个测试用新的 Motor（看门狗每个实例只能登记一次） */
static MotorConfig wheel_cfg[CHASSIS_WHEELS];
static MotorConfig steer_motor_cfg[CHASSIS_WHEELS];
static Motor pool[128];
static unsigned pool_used;
static MotorGroup group;

/* 相当于comm_rx_task.c：把收到的一帧依次交给组里的每个电机（motor_receive），有电机认领就返回 true */
static bool deliver(CanBusId bus, uint32_t id, const uint8_t *data, uint8_t len)
{
    CanFrame frame = { .id = id,
                       .len = len,
                       .stamp_us = rm_time_now_us() }; /* 中断里记下的接收时刻 */
    memcpy(frame.data, data, len);
    for (Motor *m = group.head; m != NULL; m = m->next)
    {
        if (motor_receive(m, bus, &frame))
        {
            return true;
        }
    }
    return false;
}
static Motor *wheel[CHASSIS_WHEELS];
static Motor *steer[CHASSIS_WHEELS];
static Chassis chassis;

void setUp(void)
{
    fake_can_reset();
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
    const ChassisVel target = { .vx_m_s = 1.0f };
    chassis_step(&chassis, &target, true, 1.0f, DT_S);

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
    chassis_step(&chassis, NULL, true, 1.0f, DT_S); /* 对齐到静止 */

    const ChassisVel target = { .vx_m_s = 1.0f };
    chassis_step(&chassis, &target, false, 1.0f, DT_S);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.0f * DT_S, chassis.target.velocity.vx_m_s);
    for (int k = 1; k < 100; k++)
    {
        feed_all(0.0f);
        chassis_step(&chassis, &target, false, 1.0f, DT_S);
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
    chassis_step(&chassis, NULL, true, 1.0f, DT_S);
    const ChassisVel target = { .wz_rad_s = 4.0f };
    for (int k = 0; k < 500; k++)
    {
        feed_all(0.0f);
        chassis_step(&chassis, &target, false, 1.0f, DT_S);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, wheel[0]->torque_cmd_nm); /* 已到 PID 输出限幅 */

    feed_all(0.0f);
    chassis_step(&chassis, &target, false, 0.25f, DT_S);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.75f, wheel[0]->torque_cmd_nm);
    feed_all(0.0f);
    chassis_step(&chassis, &target, false, 0.0f, DT_S);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, wheel[0]->torque_cmd_nm);
}

/* 机构停：轮 2 离线后目标改为 0，斜坡按最大减速度下降；轮 2 不写指令，其余照常闭环 */
static void test_wheel_offline_decelerates_under_control(void)
{
    feed_all(0.0f);
    chassis_step(&chassis, NULL, true, 1.0f, DT_S);
    const ChassisVel target = { .vx_m_s = 1.0f };
    for (int k = 0; k < 500; k++)
    {
        feed_all(0.0f);
        chassis_step(&chassis, &target, false, 1.0f, DT_S);
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
    chassis_step(&chassis, &target, false, 1.0f, DT_S);
    TEST_ASSERT_FALSE(chassis.measure.all_online);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, before - 2.0f * DT_S, chassis.target.velocity.vx_m_s);
    TEST_ASSERT_FALSE(wheel[2]->torque_set);
    TEST_ASSERT_TRUE(wheel[0]->torque_set);
    TEST_ASSERT_TRUE(wheel[3]->torque_set);
}

/* 麦轮：向左平移时左前、右后轮负力矩，左后、右前轮正力矩 */
static void test_mecanum_drives_by_mecanum_inverse(void)
{
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &mecanum_cfg, wheel, NULL));
    feed_all(0.0f);
    chassis_step(&c, NULL, true, 1.0f, DT_S);
    const ChassisVel target = { .vy_m_s = 1.0f };
    for (int k = 0; k < 50; k++)
    {
        feed_all(0.0f);
        chassis_step(&c, &target, false, 1.0f, DT_S);
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
    chassis_step(&c, NULL, true, 1.0f, DT_S);
    const ChassisVel target = { .vx_m_s = 0.5f, .vy_m_s = 1.0f };
    for (int k = 0; k < 50; k++)
    {
        feed_all(0.0f);
        feed_steer_all();
        chassis_step(&c, &target, false, 1.0f, DT_S);
    }
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, atan2f(1.0f, 0.5f), c.target.heading_rad[i]);
        TEST_ASSERT_TRUE(steer[i]->torque_set);
        TEST_ASSERT_TRUE(steer[i]->torque_cmd_nm > 0.0f);
        TEST_ASSERT_TRUE(wheel[i]->torque_cmd_nm > 0.0f);
    }
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

/* 舵轮：转向电机离线也算机构停，受控减速，不给它写指令 */
static void test_steer_motor_offline_is_mechanism_stop(void)
{
    add_steer_motors();
    Chassis c;
    TEST_ASSERT_TRUE(chassis_init(&c, &steer_cfg, wheel, steer));
    feed_all(0.0f);
    feed_steer_all();
    chassis_step(&c, NULL, true, 1.0f, DT_S);
    const ChassisVel target = { .vx_m_s = 1.0f };
    for (int k = 0; k < 100; k++)
    {
        feed_all(0.0f);
        feed_steer_all();
        chassis_step(&c, &target, false, 1.0f, DT_S);
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
    chassis_step(&c, &target, false, 1.0f, DT_S);
    TEST_ASSERT_FALSE(c.measure.all_online);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, before - 2.0f * DT_S, c.target.velocity.vx_m_s);
    TEST_ASSERT_FALSE(steer[3]->torque_set);
    TEST_ASSERT_TRUE(steer[0]->torque_set);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_motor_without_torque_command);
    RUN_TEST(test_stop_all_writes_nothing_and_aligns_to_measured);
    RUN_TEST(test_ramps_and_drives_wheels_by_inverse_kinematics);
    RUN_TEST(test_output_scale_limits_torque);
    RUN_TEST(test_wheel_offline_decelerates_under_control);
    RUN_TEST(test_mecanum_drives_by_mecanum_inverse);
    RUN_TEST(test_steer_turns_wheels_toward_target);
    RUN_TEST(test_steer_rejects_motor_without_torque_command);
    RUN_TEST(test_steer_motor_offline_is_mechanism_stop);
    return UNITY_END();
}
