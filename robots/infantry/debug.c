/**
 * @file    debug.c
 * @brief   步兵的心跳任务（说明见 debug.h）
 * @note    每 25 ms 一步：状态灯、电池检查、模式提示音、蜂鸣器；每 1 s 打印模式、遥控、四个轮子、底盘、IMU、电池。
 *          浮点用整数打印（RTT 的 printf 不支持 %f）：毫弧度/秒、毫米/秒、毫牛·米。
 */
#include "debug.h"

#include "config.h"
#include "core/log/log.h"
#include "core/os/os.h"
#include "devices/battery/battery.h"
#include "devices/buzzer/buzzer.h"
#include "platform/adc.h"
#include "platform/status_led.h"
#include "platform/time.h"

#define STEP_MS                25u
#define STEPS_PER_BEAT         40u /* 40 × 25 ms = 1 s 一拍 */
#define STACK_WORDS            256u
#define LED_GREEN_LEVEL        0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */
#define LOW_BATTERY_BEEP_STEPS 80u   /* 80 × 25 ms = 2 s 响一次 */

static const DebugView *view;
static const BatteryConfig battery_config = INFANTRY_BATTERY_CONFIG;
static Battery battery;
static Buzzer buzzer;
static RmTask task;
static StackType_t stack[STACK_WORDS];

/* 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次（与样板相同，用户 2026-09-28 指定） */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
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

static void log_rc(void)
{
    RcState rc;
    if (!rc_state_read(view->rc_state, &rc, RC_LOST_TIMEOUT_MS))
    {
        RM_LOG_I("rc lost (bad frames %u)", (unsigned)view->dr16->bad_frames);
        return;
    }
    RM_LOG_I("rc ch %d %d %d %d %d, sw %d %d", rc.ch[0], rc.ch[1], rc.ch[2], rc.ch[3], rc.ch[4],
             (int)rc.sw[0], (int)rc.sw[1]);
}

static void log_chassis(void)
{
    const Chassis *c = view->chassis;
    for (unsigned i = 0u; i < CHASSIS_WHEELS; i++)
    {
        const Motor *m = &view->wheel[i];
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
    if (!imu_state_read(view->imu_state, &st, IMU_STALE_MS))
    {
        RM_LOG_I("imu not ready");
        return;
    }
    RM_LOG_I("imu yaw %d pitch %d roll %d mrad, %d mC", (int)(st.yaw_rad * 1000.0f),
             (int)(st.pitch_rad * 1000.0f), (int)(st.roll_rad * 1000.0f),
             (int)(st.temperature_c * 1000.0f));
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
    const RobotMode mode = view->gate->mode;
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

static void heartbeat_entry(void *arg)
{
    (void)arg;
    uint32_t beat = 0u;
    uint32_t step = 0u;
    uint32_t tick = 0u;
    RobotMode last_mode = view->gate->mode;

    if (buzzer_init(&buzzer))
    {
        buzzer_play(&buzzer,
                    BUZZER_STARTUP); /* 心跳任务在 startup 任务结束后才运行，此时启动已完成 */
    }
    else
    {
        RM_LOG_E("buzzer pwm start failed");
    }

    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);
        check_battery(tick);
        beep_on_mode_change(&last_mode);
        buzzer_step(&buzzer, STEP_MS);
        tick++;

        if (step == 0u)
        {
            /* 每次打印都会读一次时间，同时保证 DWT 扩展计数至少每 7.8 s 更新一次（time.h 的 @pre） */
            RM_LOG_I("alive %u, mode %s", (unsigned)beat, mode_name(view->gate->mode));
            log_rc();
            log_chassis();
            log_imu();
            RM_LOG_I("battery %d mV%s", (int)(battery.voltage_v * 1000.0f),
                     battery.low ? " (low)" : "");
            beat++;
        }

        step = (step + 1u) % STEPS_PER_BEAT;
        rm_task_delay_until(&last_wake, STEP_MS);
    }
}

void debug_create_task(uint32_t priority, const DebugView *v)
{
    view = v;
    battery_init(&battery, &battery_config);
    if (!rm_task_create(&task, "heartbeat", heartbeat_entry, NULL, priority, stack, STACK_WORDS))
    {
        RM_LOG_E("create heartbeat task failed");
    }
}
