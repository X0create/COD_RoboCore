/**
 * @file    config.h
 * @brief   台架验证固件的固定参数：遥控 → 底盘电机转速（照搬 COD-H7-Template `Control_Task.c`）
 * @note    旧工程在电调原始单位下整定：目标 = 遥控通道 3 × 5（转子 rpm），PID 输出是电流原始值
 *          （kp 13、ki 0.1、kd 0、积分限幅 5000、输出限幅 12000）。本模板的电机接口是输出轴国际单位（ADR 0031），
 *          下面按固定比例换算，控制行为与旧工程等价（tests/host/app/test_bench_config.c 逐步比对）：
 *          - 速度：1 rad/s（输出轴）= 减速比 × 60 / 2π rpm（转子）
 *          - 力矩：1 N·m（输出轴）= 16384 / (20 A × 0.3 N·m/A) 电流原始值（M3508 原装减速箱，附录 A.2）
 *          PID 仍不带 dt（ADR 0029），参数和 1 kHz 调用频率绑定。
 */
#pragma once

#include "algorithm/control/pid.h"
#include "devices/motor/motor.h"

#define BENCH_TWO_PI 6.28318530718f

/** 1 rad/s 输出轴对应的转子 rpm */
#define BENCH_RPM_PER_RAD_S (DJI_M3508_GEAR_RATIO * 60.0f / BENCH_TWO_PI)
/** 1 N·m 输出轴对应的 C620 电流原始值 */
#define BENCH_RAW_PER_NM (16384.0f / (20.0f * 0.3f))

/** 遥控通道 3 每一格对应的输出轴目标转速（旧工程：5 rpm 转子） */
#define BENCH_SPEED_PER_CH (5.0f / BENCH_RPM_PER_RAD_S)

/** 旧工程底盘速度环参数，换算到国际单位 */
#define BENCH_SPEED_PID_PARAM                                                                      \
    ((PidParam){ .kp = 13.0f * BENCH_RPM_PER_RAD_S / BENCH_RAW_PER_NM,                             \
                 .ki = 0.1f * BENCH_RPM_PER_RAD_S / BENCH_RAW_PER_NM,                              \
                 .kd = 0.0f,                                                                       \
                 .d_alpha = 0.0f,                                                                  \
                 .deadband = 0.0f,                                                                 \
                 .integral_limit = 5000.0f / BENCH_RPM_PER_RAD_S,                                  \
                 .output_limit = 12000.0f / BENCH_RAW_PER_NM })

/**
 * 电池（6S）：连续 1 s 低于 21.0 V 提示低电量，回到 21.5 V 以上解除（ADR 0038）；
 * 分压比 11 取自 COD-H7-Template bsp_adc.c，待万用表核对（V16）
 */
#define BENCH_BATTERY_CONFIG                                                                       \
    {                                                                                              \
        .divider = 11.0f, .low_v = 21.0f, .recover_v = 21.5f, .hold_ms = 1000u                     \
    }

/** 解锁 / 急停拨杆：右拨杆 sw[1]（ADR 0032；左右以 V11 上板核对为准） */
#define BENCH_ARM_SWITCH 1u

/**
 * IMU 安装旋转：机体系向量 = R × 芯片系向量（按行存储，ADR 0006）。
 * 默认芯片轴与机体轴同向（X 前、Y 左、Z 上）；实际朝向以 V7 上板结果为准，改这里即可。
 */
#define BENCH_IMU_INSTALL_ROTATION                                                                 \
    {                                                                                              \
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f                                       \
    }
