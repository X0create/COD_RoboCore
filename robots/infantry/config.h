/**
 * @file    config.h
 * @brief   步兵的固定参数：底盘（第一版：四轮全向轮，遥控直接给底盘速度）
 * @note    标 “待量” 的尺寸按实车量好再改；标 “待核对” 的方向在台架上按 docs/VERIFICATION_TODO.md 核对。
 *          换轮组：改 INFANTRY_CHASSIS_CONFIG 的 .type 并填对应的尺寸（全向轮 .omni、麦轮 .mecanum、
 *          舵轮 .steer，含义见 algorithm/kinematics/ 下的头文件）；舵轮还要在 robot.c 里加 4 个转向电机。
 *          PID 不带 dt（ADR 0029），参数和 1 kHz 调用频率绑定。
 */
#pragma once

#include "algorithm/control/pid.h"
#include "devices/motor/motor.h"
#include "subsystems/chassis/chassis.h"

#define INFANTRY_PI 3.14159265359f

/*
 * 驱动轮速度环：沿用旧工程（COD-H7-Template Control_Task.c）的底盘速度环参数，换算到国际单位，
 * 换算方法与 robots/_template/config.h 相同（kp 13、ki 0.1、积分限幅 5000 rpm、输出限幅 12000 电流原始值）
 */
#define INFANTRY_RPM_PER_RAD_S (DJI_M3508_GEAR_RATIO * 60.0f / (2.0f * INFANTRY_PI))
#define INFANTRY_RAW_PER_NM    (16384.0f / (20.0f * 0.3f))
#define INFANTRY_DRIVE_SPEED_PID                                                                   \
    {                                                                                              \
        .kp = 13.0f * INFANTRY_RPM_PER_RAD_S / INFANTRY_RAW_PER_NM,                                \
        .ki = 0.1f * INFANTRY_RPM_PER_RAD_S / INFANTRY_RAW_PER_NM,                                 \
        .integral_limit = 5000.0f / INFANTRY_RPM_PER_RAD_S,                                        \
        .output_limit = 12000.0f / INFANTRY_RAW_PER_NM                                             \
    }

/**
 * 底盘：X 形四轮全向轮，轮 0–3 = 左前、左后、右后、右前（omni.h）。
 * 轮半径、中心距待量；加速度先取保守值，台架整定
 */
#define INFANTRY_CHASSIS_CONFIG                                                                    \
    {                                                                                              \
        .type = CHASSIS_OMNI,                                                                      \
        .omni = { .wheel_radius_m = 0.076f,                                                        \
                  .center_dist_m = 0.25f,                                                          \
                  .first_wheel_rad = INFANTRY_PI / 4.0f },                                         \
        .drive_speed_pid = INFANTRY_DRIVE_SPEED_PID, .max_accel_m_s2 = 2.0f,                       \
        .max_alpha_rad_s2 = 4.0f                                                                   \
    }

/*
 * 四个驱动轮电机：FDCAN1（经典 CAN），电调 ID 1–4 依次对应轮 0–3（左前、左后、右后、右前），按实车接线改。
 * direction：使电机正转 = 这个轮子推动底盘逆时针转（omni.h）。四个电机轴都朝外对称安装时四个值相同，
 * 正负待核对
 */
#define INFANTRY_WHEEL_CAN_BUS   CAN_BUS_1
#define INFANTRY_WHEEL_DIRECTION 1

/*
 * 遥控 → 底盘速度（满杆时的速度）。第一版底盘直接读遥控（ADR 0043）：
 * 左摇杆上下 ch[3] → 前后，左摇杆左右 ch[2] → 左右，右摇杆左右 ch[0] → 旋转；
 * 通道对应沿用旧工程编号，待 V11 核对。首次上台架先用小值
 */
#define INFANTRY_MAX_VX_M_S   1.0f
#define INFANTRY_MAX_VY_M_S   1.0f
#define INFANTRY_MAX_WZ_RAD_S 2.0f

/**
 * 电池（6S）：连续 1 s 低于 21.0 V 提示低电量，回到 21.5 V 以上解除（ADR 0038）；
 * 分压比 11 取自 COD-H7-Template bsp_adc.c，待万用表核对（V16）
 */
#define INFANTRY_BATTERY_CONFIG                                                                    \
    {                                                                                              \
        .divider = 11.0f, .low_v = 21.0f, .recover_v = 21.5f, .hold_ms = 1000u                     \
    }

/** 解锁 / 急停拨杆：右拨杆 sw[1]（ADR 0032；左右以 V11 上板核对为准） */
#define INFANTRY_ARM_SWITCH 1u

/**
 * IMU 安装旋转：机体系向量 = R × 芯片系向量（按行存储，ADR 0006）。
 * 默认芯片轴与机体轴同向（X 前、Y 左、Z 上）；实际朝向以 V7 上板结果为准
 */
#define INFANTRY_IMU_INSTALL_ROTATION                                                              \
    {                                                                                              \
        1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f                                       \
    }
