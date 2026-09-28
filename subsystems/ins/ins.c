/**
 * @file    ins.c
 * @brief   惯性导航子系统，见 ins.h
 */
#include "ins.h"

#define TWO_PI_F 6.28318530718f
#define PI_F     3.14159265359f

/* 旧工程 INS_Task.c 的参数 */
#define EKF_Q_QUAT  10.0f
#define EKF_Q_BIAS  0.001f
#define EKF_R_ACCEL 1000000.0f
#define EKF_DT_S    0.001f /* 旧工程固定 1 ms（CHANGES“计划中”：改用实测 dt） */

/* 加速度模长小于这个值的读数当作坏帧（静止时约 9.8 m/s²） */
#define ACCEL_MIN_M_S2 1.0f

/* 加速度二阶低通系数（旧工程 INS_LPF2p_Alpha） */
static const float accel_lpf_coef[3] = { 1.929454039488895f, -0.93178349823448126f,
                                         0.002329458745586203f };

bool ins_init(Ins *ins, const InsConfig *cfg, ImuStateTopic *out)
{
    *ins = (Ins){ .cfg = cfg, .out = out, .phase = INS_PHASE_CALIBRATING };
    gyro_bias_reset(&ins->calib);
    quat_ekf_init(&ins->ekf, EKF_Q_QUAT, EKF_Q_BIAS, EKF_R_ACCEL);
    for (int i = 0; i < 3; i++)
    {
        lpf2_init(&ins->accel_lpf[i], accel_lpf_coef);
    }
    return imu_state_claim(out, "ins");
}

Bmi088Status ins_start(Ins *ins)
{
    return bmi088_init(&ins->imu);
}

static void rotate(const float r[9], const float in[3], float out[3])
{
    for (int i = 0; i < 3; i++)
    {
        out[i] = r[3 * i] * in[0] + r[3 * i + 1] * in[1] + r[3 * i + 2] * in[2];
    }
}

static InsEvent calibrate_step(Ins *ins, const Bmi088Sample *s)
{
    gyro_bias_add(&ins->calib, s->gyro_rad_s);
    if (ins->calib.count < INS_CALIB_SAMPLES)
    {
        return INS_EVENT_NONE;
    }

    float bias[3];
    const GyroBiasResult r = gyro_bias_result(&ins->calib, INS_CALIB_SAMPLES, INS_CALIB_MAX_STD,
                                              INS_CALIB_MAX_BIAS, bias);
    gyro_bias_reset(&ins->calib);
    switch (r)
    {
        case GYRO_BIAS_OK:
            bmi088_set_gyro_offset(&ins->imu, bias);
            ins->phase = INS_PHASE_RUNNING;
            return INS_EVENT_CALIBRATED;
        case GYRO_BIAS_TOO_LARGE:
            return INS_EVENT_CALIB_BIAS_TOO_LARGE;
        case GYRO_BIAS_NOT_STILL:
        case GYRO_BIAS_TOO_FEW:
            break;
    }
    return INS_EVENT_CALIB_NOT_STILL;
}

static void run_step(Ins *ins, const Bmi088Sample *s)
{
    ImuState st;
    float accel_body[3];
    rotate(ins->cfg->install_rotation, s->gyro_rad_s, st.gyro_rad_s);
    rotate(ins->cfg->install_rotation, s->accel_m_s2, accel_body);
    for (int i = 0; i < 3; i++)
    {
        st.accel_m_s2[i] = lpf2_update(&ins->accel_lpf[i], accel_body[i]);
    }

    quat_ekf_update(&ins->ekf, st.gyro_rad_s, st.accel_m_s2, EKF_DT_S);

    for (int i = 0; i < 4; i++)
    {
        st.q[i] = ins->ekf.q[i];
    }
    quat_to_euler(st.q, &st.yaw_rad, &st.pitch_rad, &st.roll_rad);

    /* 多圈航向：相邻两次跨过 ±π 就计一圈（旧工程同样做法，单位由度改为弧度） */
    if (ins->have_yaw)
    {
        const float d = st.yaw_rad - ins->last_yaw_rad;
        if (d < -PI_F)
        {
            ins->yaw_turns++;
        }
        else if (d > PI_F)
        {
            ins->yaw_turns--;
        }
    }
    ins->have_yaw = true;
    ins->last_yaw_rad = st.yaw_rad;
    st.yaw_total_rad = st.yaw_rad + (float)ins->yaw_turns * TWO_PI_F;
    st.temperature_c = s->temperature_c;

    imu_state_publish(ins->out, &st);
}

InsEvent ins_step(Ins *ins)
{
    Bmi088Sample s;
    const bool ok = bmi088_read(&ins->imu, &s);
    const float a2 = ok ? s.accel_m_s2[0] * s.accel_m_s2[0] + s.accel_m_s2[1] * s.accel_m_s2[1]
                              + s.accel_m_s2[2] * s.accel_m_s2[2]
                        : 0.0f;
    if (!ok || a2 < ACCEL_MIN_M_S2 * ACCEL_MIN_M_S2)
    {
        ins->read_failures++;
        bmi088_heater_off(&ins->imu);
        return INS_EVENT_READ_FAILED;
    }

    bmi088_heater_step(&ins->imu, s.temperature_c);
    if (ins->phase == INS_PHASE_CALIBRATING)
    {
        return calibrate_step(ins, &s);
    }
    run_step(ins, &s);
    return INS_EVENT_NONE;
}
