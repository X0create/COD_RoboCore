/**
 * @file    config.h
 * @brief   样板兵种的固定参数：遥控 → 底盘电机转速（照搬 COD-H7-Template `Control_Task.c`）
 * @note    旧工程在电调原始单位下整定：目标 = 遥控通道 3 × 5（转子 rpm），PID 输出是电流原始值
 *          （kp 13、ki 0.1、kd 0、积分限幅 5000、输出限幅 12000）。本模板的电机接口是输出轴国际单位（ADR 0031），
 *          下面按固定比例换算，控制行为与旧工程等价（tests/host/robots/test_template_config.c 逐步比对）：
 *          - 速度：1 rad/s（输出轴）= 减速比 × 60 / 2π rpm（转子）
 *          - 力矩：1 N·m（输出轴）= 16384 / (20 A × 0.3 N·m/A) 电流原始值（M3508 原装减速箱，附录 A.2）
 *          PID 仍不带 dt（ADR 0029），参数和 1 kHz 调用频率绑定。
 */
#pragma once

#include "algorithm/control/pid.h"
#include "devices/motor/motor.h"

#define TEMPLATE_TWO_PI 6.28318530718f

/** 1 rad/s 输出轴对应的转子 rpm */
#define TEMPLATE_RPM_PER_RAD_S (DJI_M3508_GEAR_RATIO * 60.0f / TEMPLATE_TWO_PI)
/** 1 N·m 输出轴对应的 C620 电流原始值 */
#define TEMPLATE_RAW_PER_NM (16384.0f / (20.0f * 0.3f))

/** 遥控通道 3 每一格对应的输出轴目标转速（旧工程：5 rpm 转子） */
#define TEMPLATE_SPEED_PER_CH (5.0f / TEMPLATE_RPM_PER_RAD_S)

/** 旧工程底盘速度环参数，换算到国际单位 */
#define TEMPLATE_SPEED_PID_PARAM                                                                   \
    ((PidParam){ .kp = 13.0f * TEMPLATE_RPM_PER_RAD_S / TEMPLATE_RAW_PER_NM,                       \
                 .ki = 0.1f * TEMPLATE_RPM_PER_RAD_S / TEMPLATE_RAW_PER_NM,                        \
                 .kd = 0.0f,                                                                       \
                 .d_alpha = 0.0f,                                                                  \
                 .deadband = 0.0f,                                                                 \
                 .integral_limit = 5000.0f / TEMPLATE_RPM_PER_RAD_S,                               \
                 .output_limit = 12000.0f / TEMPLATE_RAW_PER_NM })

/** 解锁 / 急停拨杆：右拨杆 sw[1]（ADR 0032；左右以 V11 上板核对为准） */
#define TEMPLATE_ARM_SWITCH 1u
