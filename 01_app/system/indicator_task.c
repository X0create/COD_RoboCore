/**
 * @file    indicator_task.c
 * @brief   indicator 任务，见 indicator_task.h
 * @note    状态灯、蜂鸣器、电池的对象和参数只在本文件。
 */
#include "indicator_task.h"

#include "01_app/system/safety_gate.h"
#include "02_devices/battery/battery.h"
#include "02_devices/buzzer/buzzer.h"
#include "04_core/log/log.h"
#include "04_core/os/os.h"
#include "05_platform/adc/adc.h"
#include "05_platform/status_led/status_led.h"
#include "05_platform/time/time.h"

#define STEP_MS                25u
#define STEPS_PER_BEAT         40u   /* 40 × 25 ms = 1 s 一拍 */
#define LED_GREEN_LEVEL        0x20u /* WS2812 满亮度很刺眼，1/8 亮度足够看清 */
#define LOW_BATTERY_BEEP_STEPS 80u   /* 80 × 25 ms = 2 s 响一次 */

/*
 * 电池（6S）：连续 1 s 低于 21.0 V 提示低电量，回到 21.5 V 以上解除（ADR 0038）。
 * 分压比 11 是 DM-MC02 板上的电阻分压，取自 COD-H7-Template bsp_adc.c，待万用表核对（V16）
 */
static const BatteryConfig battery_config = {
    .divider = 11.0f, .low_v = 21.0f, .recover_v = 21.5f, .hold_ms = 1000u
};

static Battery battery; /* 只在本任务里写；log 任务通过 indicator_battery_*() 读 */
static Buzzer buzzer;

/*
 * 绿色每拍闪两下表示正常：0–50 ms 亮一次，200–225 ms 再亮一次，其余时间灭。
 * 两次亮的时长不同（50 ms、25 ms，用户 2026-09-28 指定），一长一短容易和其他闪烁码区分。
 */
static bool led_on_at(uint32_t step)
{
    return step < 2u || step == 8u;
}

/* 低电量期间每 2 s 响一次；状态变化时打印一次 */
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
    const RobotMode mode = safety_gate.mode;
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

void indicator_task_entry(void *arg)
{
    (void)arg;
    battery_init(&battery, &battery_config);
    if (!adc_start())
    {
        RM_LOG_E("adc start failed"); /* 只影响低电量提示，不阻止解锁 */
    }
    if (buzzer_init(&buzzer))
    {
        buzzer_play(&buzzer, BUZZER_STARTUP);
    }
    else
    {
        RM_LOG_E("buzzer pwm start failed");
    }

    uint32_t step = 0u;
    uint32_t tick = 0u;
    RobotMode last_mode = safety_gate.mode;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        rm_status_led_set(0u, led_on_at(step) ? LED_GREEN_LEVEL : 0u, 0u);
        check_battery(tick);
        beep_on_mode_change(&last_mode);
        buzzer_step(&buzzer, STEP_MS);

        tick++;
        step = (step + 1u) % STEPS_PER_BEAT;
        rm_task_delay_until(&last_wake, STEP_MS);
    }
}

float indicator_battery_v(void)
{
    return battery.voltage_v;
}

bool indicator_battery_low(void)
{
    return battery.low;
}
