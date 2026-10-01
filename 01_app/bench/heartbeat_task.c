/**
 * @file    heartbeat_task.c
 * @brief   台架验证固件的心跳任务：状态灯、蜂鸣器（启动音、解锁 / 上锁音、低电量每 2 s 两声）、电池检查、
 *          每秒通过 RTT 打印模式、遥控、电机反馈、图传、USB、IMU 和电池
 * @note    只读控制相关的对象（robot.h），不参与控制；打印的数值只供观察。
 *          上线 / 离线的变化由 daemon 任务打印。浮点用整数打印（RTT 的 printf 不支持 %f）。
 */
#include "robot.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "05_platform/adc.h"
#include "05_platform/status_led.h"
#include "05_platform/time.h"
#include "05_platform/usb_cdc.h"

#define HEARTBEAT_STEP_MS      25u
#define HEARTBEAT_STEPS        40u   /* 40 × 25 ms = 1 s 一拍 */
#define LED_GREEN_LEVEL        0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */
#define LOW_BATTERY_BEEP_STEPS 80u   /* 80 × 25 ms = 2 s 响一次 */

/*
 * 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次，其余时间灭。
 * 两次亮的时长不同（50 ms、25 ms，用户 2026-09-28 指定），一长一短容易和其他闪烁码区分。
 */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
}

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

static const char *mode_name(RobotMode mode)
{
    switch (mode)
    {
        case ROBOT_MODE_INIT:
            return "init";
        case ROBOT_MODE_SAFE:
            return "safe";
        case ROBOT_MODE_MANUAL:
            return "manual";
    }
    return "?";
}

/* 电池：低电量期间每 2 s 响一次；状态变化时打印一次 */
static void check_battery(uint32_t tick)
{
    const bool was_low = battery.low;
    const bool low = battery_update(&battery, adc_read_volts(ADC_BATTERY), rm_time_now_us());
    if (low != was_low)
    {
        if (low)
        {
            RM_LOG_W("battery low: %d mV", (int)(battery.voltage_v * 1000.0f));
        }
        else
        {
            RM_LOG_I("battery ok: %d mV", (int)(battery.voltage_v * 1000.0f));
        }
    }
    if (low && tick % LOW_BATTERY_BEEP_STEPS == 0u)
    {
        buzzer_play(&buzzer, BUZZER_LOW_BATTERY);
    }
}

/* 模式变化时的提示音：进入 Manual 为解锁音，离开为上锁音 */
static void beep_on_mode_change(RobotMode *last)
{
    const RobotMode mode = gate.mode;
    if (mode != *last)
    {
        if (mode == ROBOT_MODE_MANUAL)
        {
            buzzer_play(&buzzer, BUZZER_ARM);
        }
        else if (*last == ROBOT_MODE_MANUAL)
        {
            buzzer_play(&buzzer, BUZZER_DISARM);
        }
        *last = mode;
    }
}

void heartbeat_task_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;
    uint32_t tick = 0u;
    RobotMode last_mode = gate.mode;

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);
        check_battery(tick);
        beep_on_mode_change(&last_mode);
        buzzer_step(&buzzer, HEARTBEAT_STEP_MS);
        tick++;

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u, mode %s", (unsigned)beat, mode_name(gate.mode));
            log_rc();
            log_motor();
            log_joint();
            log_vt_link();
            log_usb();
            log_imu();
            RM_LOG_I("battery %d mV%s", (int)(battery.voltage_v * 1000.0f),
                     battery.low ? " (low)" : "");
            beat++;
        }

        step = (step + 1u) % HEARTBEAT_STEPS;
        rm_task_delay_until(&last_wake, HEARTBEAT_STEP_MS);
    }
}
