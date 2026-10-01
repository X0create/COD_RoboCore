/**
 * @file    ins_task.c
 * @brief   台架验证固件的 ins 任务（1 kHz）：BMI088 → 零偏标定 → EKF → 加热 → 发布 imu_state
 * @note    相当于老模板的 INS_Task.c；一个周期的具体步骤在 app/ins/ins.c 的 ins_step()。
 *          标定完成前不发布 imu_state，安全门据此全车停。
 */
#include "robot.h"

#include "core/log/log.h"
#include "core/os/delay.h"
#include "core/os/os.h"

#define INS_PERIOD_MS 1u
#define INS_RETRY_MS  1000u

static void log_ins_event(InsEvent ev, bool *failing)
{
    switch (ev)
    {
        case INS_EVENT_NONE:
            *failing = false;
            break;
        case INS_EVENT_READ_FAILED:
            if (!*failing)
            {
                RM_LOG_W("imu read failed"); /* 只在从正常变为失败时打印一次 */
            }
            *failing = true;
            break;
        case INS_EVENT_CALIBRATED:
            RM_LOG_I("gyro calibrated, imu ready");
            break;
        case INS_EVENT_CALIB_NOT_STILL:
            RM_LOG_W("gyro calibration rejected: moving, retry (keep the robot still)");
            break;
        case INS_EVENT_CALIB_BIAS_TOO_LARGE:
            RM_LOG_W("gyro calibration rejected: bias too large, retry");
            break;
    }
}

void ins_task_entry(void *arg)
{
    (void)arg;
    Bmi088Status status;
    while ((status = ins_start(&ins)) != BMI088_OK)
    {
        RM_LOG_E("bmi088 init failed (%d), retry", (int)status);
        rm_delay_ms(INS_RETRY_MS);
    }
    RM_LOG_I("bmi088 ready, calibrating gyro (keep still %u ms)", (unsigned)INS_CALIB_SAMPLES);

    bool failing = false;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        log_ins_event(ins_step(&ins), &failing);
        rm_task_delay_until(&last_wake, INS_PERIOD_MS);
    }
}
