/**
 * @file    ins.c
 * @brief   惯性导航子系统，见 ins.h
 */
#include "ins.h"
#include "03_algorithm/math/math_const.h"

#include "05_platform/time/time.h"

/* 旧工程 INS_Task.c 的参数 */
#define EKF_Q_QUAT  10.0f
#define EKF_Q_BIAS  0.001f
#define EKF_R_ACCEL 1000000.0f
#define EKF_DT_S    0.001f /* 第一次更新没有上一次的时刻，用标称周期 */

/* 加速度模长小于这个值的读数当作坏帧（静止时约 9.8 m/s²） */
#define ACCEL_MIN_M_S2 1.0f

/* 加速度二阶低通系数（旧工程 INS_LPF2p_Alpha） */
static const float accel_lpf_coef[3] = { 1.929454039488895f, -0.93178349823448126f,
                                         0.002329458745586203f };

void ins_init(Ins *ins, const InsConfig *cfg)
{
    *ins = (Ins){ .cfg = cfg, .phase = INS_PHASE_CALIBRATING };
    gyro_bias_reset(&ins->calib);
    gyro_bias_reset(&ins->still);
    quat_ekf_init(&ins->ekf, EKF_Q_QUAT, EKF_Q_BIAS, EKF_R_ACCEL);
    for (int i = 0; i < 3; i++)
    {
        lpf2_init(&ins->accel_lpf[i], accel_lpf_coef);
    }
    watchdog_register(&ins->wd, "ins", INS_TIMEOUT_MS);
}

bool ins_read(const Ins *ins, ImuState *out)
{
    return watchdog_read_data(&ins->wd, &ins->state, out, sizeof(*out));
}

Bmi088Status ins_start(Ins *ins)
{
    return bmi088_init(&ins->imu);
}

/* ins_step 的各步骤，定义在本文件后面 */
static bool read_sample(Ins *ins, Bmi088Sample *s);
static InsEvent calibrate_gyro(Ins *ins, const Bmi088Sample *s);
static ImuState update_attitude(Ins *ins, const Bmi088Sample *s, uint64_t now_us);

/* 每 1 ms 一次的完整流程（相当于老模板 INS_Task 的循环体），各步骤的细节在下面 */
InsEvent ins_step(Ins *ins)
{
    /* 1. 读 BMI088；坏帧时关加热、不发布 */
    Bmi088Sample s;
    if (!read_sample(ins, &s))
    {
        ins->read_failures++;
        bmi088_heater_off(&ins->imu);
        return INS_EVENT_READ_FAILED;
    }

    /* 2. 恒温加热：芯片温度 → 加热 PID（每 1280 ms 算一次，ADR 0042，见 bmi088.c） */
    bmi088_heater_step(&ins->imu, s.temperature_c);

    /* 3. 上电先标定陀螺零偏；标定完成前不发布，安全门据此全车停 */
    if (ins->phase == INS_PHASE_CALIBRATING)
    {
        return calibrate_gyro(ins, &s);
    }

    /* 4. 更新姿态：机体系 → 航向零偏在线修正 → 加速度低通 → EKF → 欧拉角、多圈航向 */
    const uint64_t now_us = rm_time_now_us();
    const ImuState st = update_attitude(ins, &s, now_us);

    /* 5. 保存最新姿态和它的时刻，control、log 用 ins_read() 读 */
    watchdog_feed_data(&ins->wd, &ins->state, &st, sizeof(st), now_us);
    return INS_EVENT_NONE;
}

/* ================================================================== */
/* 以下是各步骤的细节                                                    */
/* ================================================================== */

static void rotate(const float r[9], const float in[3], float out[3])
{
    for (int i = 0; i < 3; i++)
    {
        out[i] = r[3 * i] * in[0] + r[3 * i + 1] * in[1] + r[3 * i + 2] * in[2];
    }
}

/* 第 3 步（上电标定阶段）：攒满 INS_CALIB_SAMPLES 个陀螺样本，静止才把均值作为零偏，否则返回拒绝原因并重新采样 */
static InsEvent calibrate_gyro(Ins *ins, const Bmi088Sample *s)
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

/* 静止窗口满了：机体 z 轴均值小且平稳，就把一部分并入零偏。零偏是芯片系的，机体 z 分量对应安装旋转的第 3 行 */
static void track_yaw_bias(Ins *ins, const float gyro_body[3])
{
    gyro_bias_add(&ins->still, gyro_body);
    if (ins->still.count < INS_STILL_SAMPLES)
    {
        return;
    }
    float mean[3];
    const GyroBiasResult r = gyro_bias_result(&ins->still, INS_STILL_SAMPLES, INS_STILL_MAX_STD,
                                              INS_STILL_MAX_RATE, mean);
    gyro_bias_reset(&ins->still);
    if (r != GYRO_BIAS_OK)
    {
        return;
    }
    const float dz = INS_STILL_GAIN * mean[2];
    const float *r_z = &ins->cfg->install_rotation[6];
    float offset[3];
    for (int i = 0; i < 3; i++)
    {
        offset[i] = ins->imu.gyro_offset_rad_s[i] + r_z[i] * dz;
    }
    bmi088_set_gyro_offset(&ins->imu, offset);
}

/* 第 4 步：转到机体系 → 航向零偏在线修正 → 加速度低通 → EKF → 欧拉角、多圈航向 */
static ImuState update_attitude(Ins *ins, const Bmi088Sample *s, uint64_t now_us)
{
    ImuState st;
    float accel_body[3];
    rotate(ins->cfg->install_rotation, s->gyro_rad_s, st.gyro_rad_s);
    track_yaw_bias(ins, st.gyro_rad_s);
    rotate(ins->cfg->install_rotation, s->accel_m_s2, accel_body);
    for (int i = 0; i < 3; i++)
    {
        st.accel_m_s2[i] = lpf2_update(&ins->accel_lpf[i], accel_body[i]);
    }

    /* 实测两次更新的间隔（旧工程固定 1 ms）：任务被耽误或中间有读失败时，积分时间照实计算 */
    const float dt_s =
        ins->have_last_update ? (float)(now_us - ins->last_update_us) * 1e-6f : EKF_DT_S;
    ins->have_last_update = true;
    ins->last_update_us = now_us;
    quat_ekf_update(&ins->ekf, st.gyro_rad_s, st.accel_m_s2, dt_s);

    for (int i = 0; i < 4; i++)
    {
        st.q[i] = ins->ekf.q[i];
    }
    quat_ekf_to_euler(st.q, &st.yaw_rad, &st.pitch_rad, &st.roll_rad);

    /* 多圈航向：相邻两次跨过 ±π 就计一圈（旧工程同样做法，单位由度改为弧度） */
    if (ins->have_yaw)
    {
        const float d = st.yaw_rad - ins->last_yaw_rad;
        if (d < -RM_PI)
        {
            ins->yaw_turns++;
        }
        else if (d > RM_PI)
        {
            ins->yaw_turns--;
        }
    }
    ins->have_yaw = true;
    ins->last_yaw_rad = st.yaw_rad;
    st.yaw_total_rad = st.yaw_rad + (float)ins->yaw_turns * RM_TWO_PI;
    st.temperature_c = s->temperature_c;
    return st;
}

/* 第 1 步：读一次 BMI088。读失败，或加速度模长接近 0（全零会让 EKF 除零后永久变成 NaN）都算坏帧 */
static bool read_sample(Ins *ins, Bmi088Sample *s)
{
    if (!bmi088_read(&ins->imu, s))
    {
        return false;
    }
    const float a2 = s->accel_m_s2[0] * s->accel_m_s2[0] + s->accel_m_s2[1] * s->accel_m_s2[1]
                     + s->accel_m_s2[2] * s->accel_m_s2[2];
    return a2 >= ACCEL_MIN_M_S2 * ACCEL_MIN_M_S2;
}
