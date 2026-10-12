/**
 * @file    indicator_task.c
 * @brief   indicator_task（25 ms）：状态灯、蜂鸣器、低电量检查，所有车通用
 * @note    给操作手的提示都在这里：
 *            状态灯   绿灯每秒闪两下（一长一短）= 程序在正常运行；不闪 = 程序卡住或没跑起来
 *            蜂鸣器   上电响启动音；解锁、上锁各有提示音；低电量每 2 s 响一次
 *          状态灯、蜂鸣器、电池的对象和参数只在本文件；ADC、蜂鸣器 PWM 也在本任务开头打开。
 *          提示依据的状态直接读它的唯一来源，不另存副本：模式读 safety_gate.mode，电量由本任务读 ADC 判断。
 *          这些提示只给人看，不参与控制：本任务出问题不影响车动。
 */
#include "01_applic/tasks/tasks.h"

#include "01_applic/system/safety_gate.h"
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
 * 电池（6S 锂电，满电约 25.2 V）：连续 1 s 低于 21.0 V 提示低电量，回到 21.5 V 以上才解除。
 * “连续 1 s”是为了不被电机加速时的瞬间压降误触发；解除阈值高一点，避免在阈值附近反复提示。
 * 分压比 11：DM-MC02 板上把电池电压按 1/11 分压后送进 ADC。换板子或量出读数不准时，用万用表对照着改
 */
static const BatteryConfig battery_config = {
    .divider = 11.0f, .low_v = 21.0f, .recover_v = 21.5f, .hold_ms = 1000u
};

static Battery battery; /* 只在本任务里写；log_task 通过 indicator_battery_*() 读 */
static Buzzer buzzer;

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
static void beep_on_mode_change(SafetyMode *last)
{
    const SafetyMode mode = safety_gate.mode;
    if (mode != *last)
    {
        if (mode == SAFETY_MODE_MANUAL)
        {
            buzzer_play(&buzzer, BUZZER_ARM);
        }
        else if (*last == SAFETY_MODE_MANUAL)
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

    uint32_t step = 0u; /* 一拍里的第几步（0–39），决定状态灯亮灭 */
    uint32_t tick = 0u; /* 从任务开始累计的步数，决定低电量多久响一次 */
    SafetyMode last_mode = safety_gate.mode;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        /* 每 25 ms：状态灯 → 电池 → 模式提示音 → 推进蜂鸣器的音符 */
        /* 状态灯：绿色每拍闪两下表示正常——第 0–1 步（0–50 ms）亮一次，第 8 步（200–225 ms）再亮一次。
         * 两次亮的时长不同，一长一短容易和以后加的其他闪烁码区分 */
        const bool led_on = step < 2u || step == 8u;
        status_led_set(0u, led_on ? LED_GREEN_LEVEL : 0u, 0u); /* R、G、B */
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
