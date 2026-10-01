/**
 * @file    log_task.c
 * @brief   台架验证固件的 log 任务（1 s）：通过 RTT 打印模式、遥控、电机反馈、图传、USB、IMU 和电池
 * @note    只读对象（robot.h），不参与控制；打印的数值只供观察。
 *          状态灯、蜂鸣器、低电量提示在 01_app/system/indicator_task.c；上线 / 离线的变化由 detect 任务打印。
 *          浮点用整数打印（RTT 的 printf 不支持 %f）。
 */
#include "robot.h"

#include "01_app/system/indicator_task.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"

#include "05_platform/usb_cdc/usb_cdc.h"

#define LOG_PERIOD_MS 1000u

static void log_rc(void)
{
    RcState rc;
    if (!rc_state_read(&rc_state, &rc, RC_LOST_TIMEOUT_MS))
    {
        RM_LOG_I("rc lost (bad frames %u)", (unsigned)dr16.bad_frames);
        return;
    }
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d, bad frames %u", rc.ch[0], rc.ch[1], rc.ch[2],
             rc.ch[3], rc.ch[4], (int)rc.sw[0], (int)rc.sw[1], (unsigned)dr16.bad_frames);
}

/* 浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度、毫弧度/秒、毫牛·米 */
static void log_motor(void)
{
    MotorFeedback fb;
    if (!motor_read_feedback(&chassis_motor, &fb))
    {
        RM_LOG_I("m3508_1 offline");
        return;
    }
    RM_LOG_I("m3508_1 angle %d mrad, speed %d mrad/s, torque %d mNm, %d C",
             (int)(fb.angle_rad * 1000.0f), (int)(fb.speed_rad_s * 1000.0f),
             (int)(fb.torque_nm * 1000.0f), (int)fb.temperature_c);
}

static void log_vt_link(void)
{
    VtRcState rc;
    if (vt_rc_state_read(&vt_rc_state, &rc, VT_LINK_TIMEOUT_MS))
    {
        RM_LOG_I("vt13 ch %d %d %d %d, mode %d, pause %d", rc.ch[0], rc.ch[1], rc.ch[2], rc.ch[3],
                 (int)rc.mode, (int)rc.pause);
    }
    KbmState kbm;
    if (kbm_state_read(&kbm_state, &kbm, VT_LINK_TIMEOUT_MS))
    {
        RM_LOG_I("kbm keys 0x%x, mouse %d %d", (unsigned)kbm.keys, kbm.mouse_x, kbm.mouse_y);
    }
}

static void log_usb(void)
{
    static uint32_t last_bytes;
    const uint32_t bytes = usb_rx_bytes;
    RM_LOG_I("usb rx %u B/s, vision frames %u (last id 0x%x), rx dropped %u, echo dropped %u",
             (unsigned)(bytes - last_bytes), (unsigned)vision_link.frames,
             (unsigned)vision_link.last_id, (unsigned)usb_cdc_rx_dropped(),
             (unsigned)usb_echo_dropped);
    last_bytes = bytes;
}

static void log_joint(void)
{
    MotorFeedback fb;
    if (!motor_read_feedback(&joint_motor, &fb))
    {
        RM_LOG_I("dm8009_1 offline");
        return;
    }
    RM_LOG_I("dm8009_1 %s, error 0x%x, angle %d mrad, speed %d mrad/s, torque %d mNm",
             fb.enabled ? "enabled" : "disabled", (unsigned)fb.error_code,
             (int)(fb.angle_rad * 1000.0f), (int)(fb.speed_rad_s * 1000.0f),
             (int)(fb.torque_nm * 1000.0f));
}

/* 浮点用整数打印：毫弧度、毫摄氏度、微弧度每秒。零偏直接读 ins 内部（单个 float 读写是原子的，只供观察在线修正） */
static void log_imu(void)
{
    ImuState st;
    if (!imu_state_read(&imu_state, &st, IMU_STALE_MS))
    {
        RM_LOG_I("imu not ready");
        return;
    }
    const float *r_z = &ins.cfg->install_rotation[6];
    const float *off = ins.imu.gyro_offset_rad_s;
    const float bias_z = r_z[0] * off[0] + r_z[1] * off[1] + r_z[2] * off[2];
    RM_LOG_I("imu yaw %d pitch %d roll %d mrad, yaw total %d mrad, %d mC, yaw bias %d urad/s",
             (int)(st.yaw_rad * 1000.0f), (int)(st.pitch_rad * 1000.0f),
             (int)(st.roll_rad * 1000.0f), (int)(st.yaw_total_rad * 1000.0f),
             (int)(st.temperature_c * 1000.0f), (int)(bias_z * 1e6f));
    /* 机体系加速度（已低通）与角速度（已减零偏）：静止水平时约 0 0 9800 mm/s²、各轴几 mrad/s（V5、V7） */
    RM_LOG_I("imu accel %d %d %d mm/s2, gyro %d %d %d mrad/s, read failures %u",
             (int)(st.accel_m_s2[0] * 1000.0f), (int)(st.accel_m_s2[1] * 1000.0f),
             (int)(st.accel_m_s2[2] * 1000.0f), (int)(st.gyro_rad_s[0] * 1000.0f),
             (int)(st.gyro_rad_s[1] * 1000.0f), (int)(st.gyro_rad_s[2] * 1000.0f),
             (unsigned)ins.read_failures);
}

void log_task_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
        RM_LOG_I("alive %u, mode %s", (unsigned)beat, safety_gate_mode_name(safety_gate.mode));
        log_rc();
        log_motor();
        log_joint();
        log_vt_link();
        log_usb();
        log_imu();
        RM_LOG_I("battery %d mV%s", (int)(indicator_battery_v() * 1000.0f),
                 indicator_battery_low() ? " (low)" : "");
        beat++;
        rm_task_delay_until(&last_wake, LOG_PERIOD_MS);
    }
}
