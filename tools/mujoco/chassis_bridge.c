/**
 * @file    chassis_bridge.c
 * @brief   虚拟反馈 → 原安全门 / chassis_step → 原电机发送出口 → 虚拟 CAN 力矩
 */
#include "chassis_bridge.h"

#include <math.h>

#include "01_applic/config/params.h"
#include "01_applic/system/safety_gate.h"
#include "02_devices/motor/dji_motor.h"
#include "02_devices/motor/motor_group.h"
#include "sim_platform.h"

static Chassis sim_chassis;
static ChassisConfig sim_config;
static MotorGroup sim_motors;
static Motor drive[CHASSIS_WHEELS];
static Motor steer[CHASSIS_WHEELS];
static MotorConfig drive_config[CHASSIS_WHEELS];
static MotorConfig steer_config[CHASSIS_WHEELS];
static bool initialized;
static const uint32_t control_period_us = 1000u;

size_t chassis_bridge_input_size(void)
{
    return sizeof(ChassisBridgeInput);
}

size_t chassis_bridge_output_size(void)
{
    return sizeof(ChassisBridgeOutput);
}

uint32_t chassis_bridge_period_us(void)
{
    return control_period_us;
}

bool chassis_bridge_init(float radius_m, float half_wheelbase_m, float half_track_m, int diagonal)
{
    if (initialized || !isfinite(radius_m) || radius_m <= 0.0f || !isfinite(half_wheelbase_m)
        || half_wheelbase_m <= 0.0f || !isfinite(half_track_m) || half_track_m <= 0.0f
        || (diagonal != 0 && diagonal != 1))
    {
        return false;
    }
    sim_config = chassis_config; /* 保留当前固件的驱动轮 PID 和加速度参数 */
    sim_config.type = CHASSIS_HALF_STEER;
    sim_config.half_steer = (HalfSteerConfig){ .steer_diagonal = (HalfSteerDiagonal)diagonal,
                                               .steer_radius_m = radius_m,
                                               .omni_radius_m = radius_m,
                                               .half_wheelbase_m = half_wheelbase_m,
                                               .half_track_m = half_track_m };
    /* 仿真用转向参数；未用实车转向电机整定。角度环输出 rad/s，速度环输出 N·m。 */
    sim_config.steer.angle_pid = (PidParam){ .kp = 10.0f, .output_limit = 8.0f };
    sim_config.steer.speed_pid =
        (PidParam){ .kp = 0.4f, .ki = 0.001f, .integral_limit = 2000.0f, .output_limit = 2.0f };
    Motor *drive_ptr[CHASSIS_WHEELS];
    Motor *steer_ptr[CHASSIS_WHEELS] = { NULL };
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        /* 虚拟接线独立于实车方向和总线；统一 M3508 便于走原 DJI 协议与发送出口。 */
        drive_config[i] = (MotorConfig){ .name = wheel_config[i].name,
                                         .type = MOTOR_M3508,
                                         .can_bus = CAN_BUS_1,
                                         .id = (uint8_t)(i + 1u),
                                         .direction = 1,
                                         .gear_ratio = DJI_M3508_GEAR_RATIO,
                                         .stop_action = STOP_ACTION_ZERO_TORQUE };
        drive_ptr[i] = &drive[i];
        const Motor *conflict;
        if (!motor_init(&drive[i], &drive_config[i], &sim_motors, &conflict))
        {
            return false;
        }
        if (half_steer_is_steer(&sim_config.half_steer, i))
        {
            steer_config[i] = drive_config[i];
            steer_config[i].name = "sim_steer";
            steer_config[i].id = (uint8_t)(5u + i);
            steer_ptr[i] = &steer[i];
            if (!motor_init(&steer[i], &steer_config[i], &sim_motors, &conflict))
            {
                return false;
            }
        }
    }
    safety_gate_init(&safety_gate, arm_switch);
    safety_gate_set_system_ready(&safety_gate);
    initialized = chassis_init(&sim_chassis, &sim_config, drive_ptr, steer_ptr);
    return initialized;
}

static void put_be16(uint8_t *out, int16_t value)
{
    out[0] = (uint8_t)((uint16_t)value >> 8);
    out[1] = (uint8_t)((uint16_t)value & 0xFFu);
}

/* 与电调反馈一样，从虚拟输出轴换成转子编码器 / rpm，再走原来的 motor_receive。
 * 编码器计数和反馈 ID 复用 dji_motor.h，这里只做虚拟发送方的反向编码。
 * 初始转向角为 0，所以第一次反馈的零位就是模型的向前方向；不是实车零点实现。 */
static void feed_motor(Motor *motor, float angle_rad, float speed_rad_s, uint64_t now_us)
{
    const MotorConfig *cfg = motor->cfg;
    float rotor_rad = fmodf(angle_rad * cfg->gear_ratio * (float)cfg->direction, RM_TWO_PI);
    if (rotor_rad < 0.0f)
    {
        rotor_rad += RM_TWO_PI;
    }
    const int16_t encoder = (int16_t)(rotor_rad * ((float)DJI_ENCODER_COUNTS / RM_TWO_PI));
    const int16_t rpm =
        (int16_t)lroundf(speed_rad_s * DJI_M3508_RPM_PER_RAD_S * (float)cfg->direction);
    CanFrame frame = { .id = dji_feedback_id(cfg), .len = 8u, .stamp_us = now_us };
    put_be16(&frame.data[0], encoder);
    put_be16(&frame.data[2], rpm);
    frame.data[6] = 25u;
    (void)motor_receive(motor, cfg->can_bus, &frame);
}

void chassis_bridge_step(const ChassisBridgeInput *in, ChassisBridgeOutput *out)
{
    sim_platform_begin(in->now_us);
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        if ((in->feedback_mask & (1u << i)) != 0u)
        {
            feed_motor(&drive[i], in->drive_angle_rad[i], in->drive_speed_rad_s[i], in->now_us);
        }
        if (half_steer_is_steer(&sim_config.half_steer, i)
            && (in->feedback_mask & (1u << (i + 4u))) != 0u)
        {
            feed_motor(&steer[i], in->steer_angle_rad[i], in->steer_speed_rad_s[i], in->now_us);
        }
    }
    RcState rc = { 0 };
    rc.sw[arm_switch] = in->armed != 0u ? RC_SW_MID : RC_SW_DOWN;
    const SafetyDecision decision = safety_gate_update(
        &safety_gate, in->rc_online != 0u ? &rc : NULL, in->imu_ready != 0u, in->now_us);
    const ChassisVel cmd = { .vx_m_s = in->cmd[0], .vy_m_s = in->cmd[1], .wz_rad_s = in->cmd[2] };
    chassis_step(&sim_chassis, &cmd, decision.stop_all,
                 safety_gate_output_scale(&safety_gate, in->now_us),
                 (float)control_period_us * 0.000001f);
    if (decision.stop_all)
    {
        motor_group_apply_stop_all(&sim_motors);
    }
    motor_group_send(&sim_motors);
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        out->drive_torque_nm[i] = sim_platform_torque(drive_config[i].id);
        out->steer_torque_nm[i] = half_steer_is_steer(&sim_config.half_steer, i)
                                      ? sim_platform_torque(steer_config[i].id)
                                      : 0.0f;
        out->drive_target_rad_s[i] = sim_chassis.target.drive_speed_rad_s[i];
        out->heading_target_rad[i] = sim_chassis.target.heading_rad[i];
    }
    out->measure_velocity[0] = sim_chassis.measure.velocity.vx_m_s;
    out->measure_velocity[1] = sim_chassis.measure.velocity.vy_m_s;
    out->measure_velocity[2] = sim_chassis.measure.velocity.wz_rad_s;
    out->target_velocity[0] = sim_chassis.target.velocity.vx_m_s;
    out->target_velocity[1] = sim_chassis.target.velocity.vy_m_s;
    out->target_velocity[2] = sim_chassis.target.velocity.wz_rad_s;
    out->mode = (uint32_t)safety_gate.mode;
    out->all_online = sim_chassis.measure.all_online ? 1u : 0u;
}
