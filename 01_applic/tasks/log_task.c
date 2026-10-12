/**
 * @file    log_task.c
 * @brief   这台车的 log_task（1 s）：通过 RTT 打印模式、遥控、四个轮子、底盘目标、IMU、超级电容、电池、控制实时性
 * @note    RTT（Real Time Transfer）：J-Link 通过调试口读写单片机内存来传文字，不占串口、几乎不耽误程序。
 *          在 Ozone 的 Terminal 窗口或 J-Link RTT Viewer 里就能看到打印。
 *          想多看一个量：照下面 log_xxx() 的写法加一个函数，在循环里调用。
 *          只读对象（robot.h），不参与控制；打印的数值只供观察，单个 float 读写是原子的，但不同字段可能来自不同周期。
 *          状态灯、蜂鸣器、低电量提示在 01_applic/tasks/indicator_task.c；上线 / 离线的变化由 detect_task 打印。
 *          浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度/秒、毫米/秒、毫牛·米。
 */
#include "01_applic/tasks/tasks.h"

#include "01_applic/config/robot.h"

#include "04_core/log/log.h"
#include "04_core/os/os.h"

#define LOG_PERIOD_MS 1000u

/* 遥控：在线时打印 5 个通道和两个拨杆；丢失时打印坏帧数（排查接线、波特率时看它是否在涨） */
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

/* 每个轮子一行：实测转速 / 目标转速 / 力矩 / 温度；最后一行是底盘目标速度 */
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
             c->measure.all_online ? "" : " (wheel offline: degraded)");
}

/* 姿态（欧拉角只用于显示）和 IMU 温度；标定完成前打印 not ready */
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

/* 超级电容：电压、底盘功率、剩余能量、是否在输出、错误标志；离线时打印收到的坏帧数（协议版本不一致时会涨） */
static void log_supercap(void)
{
    SupercapFeedback fb;
    if (!supercap_read(&supercap, &fb))
    {
        RM_LOG_I("supercap offline (bad frames %u)", (unsigned)supercap.bad_frames);
        return;
    }
    RM_LOG_I("supercap %d mV, chassis %d mW, energy %d%%, output %d, error 0x%02x",
             (int)(fb.cap_voltage_v * 1000.0f), (int)(fb.chassis_power_w * 1000.0f),
             (int)(fb.energy_ratio * 100.0f), (int)fb.output_enabled, (unsigned)fb.error_flags);
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
        log_supercap();
        RM_LOG_I("battery %d mV%s", (int)(indicator_battery_v() * 1000.0f),
                 indicator_battery_low() ? " (low)" : "");
        /* control_task 实时性：最近 1 s 的最大执行时间、最大周期间隔（应约为 1000 us），超期次数自上电累计。
         * late 在涨说明 control 被别的任务或中断拖慢了 */
        const ControlTiming t = control_timing_take();
        RM_LOG_I("control exec max %u us, period max %u us, late %u", (unsigned)t.exec_max_us,
                 (unsigned)t.period_max_us, (unsigned)t.late_count);
        beat++;
        rm_task_delay_until(&last_wake, LOG_PERIOD_MS);
    }
}
