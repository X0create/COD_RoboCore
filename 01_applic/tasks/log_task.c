/**
 * @file    log_task.c
 * @brief   这台车的 log_task（1 s）：通过 RTT 打印模式、遥控、四个轮子、底盘目标、IMU、电池
 * @note    只读对象（robot.h），不参与控制；打印的数值只供观察，单个 float 读写是原子的，但不同字段可能来自不同周期。
 *          状态灯、蜂鸣器、低电量提示在 01_applic/tasks/indicator_task.c；上线 / 离线的变化由 detect_task 打印。
 *          浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度/秒、毫米/秒、毫牛·米。
 */
#include "01_applic/robot/robot.h"

#include "01_applic/tasks/indicator_task.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"

#define LOG_PERIOD_MS 1000u

static void log_rc(void)
{
    RcState rc;
    if (!dr16_read(&dr16, &rc))
    {
        RM_LOG_I("rc lost (bad frames %u)", (unsigned)dr16.bad_frames);
        return;
    }
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d", rc.ch[0], rc.ch[1], rc.ch[2], rc.ch[3], rc.ch[4],
             (int)rc.sw[0], (int)rc.sw[1]);
}

static void log_chassis(void)
{
    const Chassis *c = &chassis;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *m = &wheel_motor[i];
        MotorFeedback fb;
        if (!motor_read_feedback(m, &fb))
        {
            RM_LOG_I("%s offline", m->cfg->name);
            continue;
        }
        RM_LOG_I("%s speed %d mrad/s (target %d), torque %d mNm, %d C", m->cfg->name,
                 (int)(fb.speed_rad_s * 1000.0f), (int)(c->target.drive_speed_rad_s[i] * 1000.0f),
                 (int)(fb.torque_nm * 1000.0f), (int)fb.temperature_c);
    }
    RM_LOG_I("chassis target vx %d vy %d mm/s, wz %d mrad/s%s",
             (int)(c->target.velocity.vx_m_s * 1000.0f), (int)(c->target.velocity.vy_m_s * 1000.0f),
             (int)(c->target.velocity.wz_rad_s * 1000.0f),
             c->measure.all_online ? "" : " (motor offline: stopping)");
}

static void log_imu(void)
{
    ImuState st;
    if (!ins_read(&ins, &st))
    {
        RM_LOG_I("imu not ready");
        return;
    }
    RM_LOG_I("imu yaw %d pitch %d roll %d mrad, %d mC", (int)(st.yaw_rad * 1000.0f),
             (int)(st.pitch_rad * 1000.0f), (int)(st.roll_rad * 1000.0f),
             (int)(st.temperature_c * 1000.0f));
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
        log_chassis();
        log_imu();
        RM_LOG_I("battery %d mV%s", (int)(indicator_battery_v() * 1000.0f),
                 indicator_battery_low() ? " (low)" : "");
        beat++;
        rm_task_delay_until(&last_wake, LOG_PERIOD_MS);
    }
}
