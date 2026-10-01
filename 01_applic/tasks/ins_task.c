/**
 * @file    ins_task.c
 * @brief   ins_task，见 ins_task.h
 */
#include "ins_task.h"

#include "01_applic/modules/ins/ins.h"

#include "04_core/log/log.h"
#include "04_core/os/delay.h"
#include "04_core/os/os.h"

#define INS_PERIOD_MS 1u
#define INS_RETRY_MS  1000u

/* 把 ins_step() 返回的事件写进日志。failing 记住“上一次是否读失败”，连续失败只打印一次，避免 1 kHz 刷屏 */
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
    Ins *ins = arg;
    Bmi088Status status;
    /* BMI088 初始化失败（没接、片选错）就每秒重试；这期间 IMU 未就绪，安全门保持全车停 */
    while ((status = ins_start(ins)) != BMI088_OK)
    {
        RM_LOG_E("bmi088 init failed (%d), retry", (int)status);
        rm_delay_ms(INS_RETRY_MS);
    }
    RM_LOG_I("bmi088 ready, calibrating gyro (keep still %u ms)", (unsigned)INS_CALIB_SAMPLES);

    bool failing = false;
    RmTaskPeriod last_wake = rm_task_period_start();
    for (;;)
    {
        const InsEvent ev = ins_step(ins); /* 读 BMI088 → 加热 → 标定或更新姿态 → 保存最新姿态 */
        log_ins_event(ev, &failing);
        rm_task_delay_until(&last_wake, INS_PERIOD_MS);
    }
}
