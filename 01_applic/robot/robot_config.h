/**
 * @file    robot_config.h
 * @brief   这台车的全部可调参数（目前：四轮全向轮底盘，遥控直接给底盘速度）
 * @note    这台车的参数都在这里，robot.c 只用这里的配置表创建对象。通用的参数（电池阈值、EKF、IMU 加热、
 *          各种超时）留在各自模块里，位置见 01_applic/README.md“参数在哪里”。
 *          标 “待量” 的尺寸按实车量好再改；标 “待核对” 的方向在台架上按 docs/VERIFICATION_TODO.md 核对。
 *          换轮组：改 chassis_config 的 .type 并填对应的尺寸（全向轮 .omni、麦轮 .mecanum、
 *          舵轮 .steer，含义见 03_algorithm/kinematics/ 下的头文件）；舵轮还要在 robot.c 里加 4 个转向电机。
 *          PID 不带 dt（ADR 0029），参数和 1 kHz 调用频率绑定。
 *          这里的表是 static const：只给 robot.c 和 01_applic/tasks/ 里的任务用，没用到的不占空间。
 */
#pragma once

#include <stdint.h>

#include "01_applic/modules/chassis/chassis.h"
#include "01_applic/modules/ins/ins.h"
#include "02_devices/motor/motor.h"
#include "03_algorithm/math/math_const.h"

/* ------------------------------------------------------------------ */
/* 驱动轮电机：一行一个轮子，按实车接线改                                */
/* ------------------------------------------------------------------ */

/*
 * 顺序 = 底盘的轮 0–3（左前、左后、右后、右前，omni.h），全部在 FDCAN1（经典 CAN）。
 * direction：使电机正转 = 这个轮子推动底盘逆时针转（omni.h）；四个电机轴都朝外对称安装时四个值相同，正负待核对（V46）。
 * 停机动作：零力矩（DJI 电调只支持零力矩 / 失能，ADR 0031）。
 */
static const MotorConfig wheel_config[CHASSIS_WHEELS] = {
    { .name = "wheel_lf",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 1u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = SAFE_ACTION_ZERO_TORQUE },
    { .name = "wheel_lb",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 2u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = SAFE_ACTION_ZERO_TORQUE },
    { .name = "wheel_rb",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 3u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = SAFE_ACTION_ZERO_TORQUE },
    { .name = "wheel_rf",
      .type = MOTOR_M3508,
      .can_bus = CAN_BUS_1,
      .id = 4u,
      .direction = 1,
      .gear_ratio = DJI_M3508_GEAR_RATIO,
      .stop_action = SAFE_ACTION_ZERO_TORQUE },
};

/* ------------------------------------------------------------------ */
/* 底盘：尺寸、加速度、每轮速度环                                        */
/* ------------------------------------------------------------------ */

/*
 * X 形四轮全向轮。轮半径、中心距待量；加速度先取保守值，台架整定。
 * 速度环沿用旧工程（COD-H7-Template Control_Task.c）的底盘参数，换算到国际单位：
 * kp 13、ki 0.1、积分限幅 5000 rpm、输出限幅 12000 电流原始值（换算常数在 02_devices/motor/motor.h）
 */
static const ChassisConfig chassis_config = {
    .type = CHASSIS_OMNI,
    .omni = { .wheel_radius_m = 0.076f, .center_dist_m = 0.25f, .first_wheel_rad = RM_PI / 4.0f },
    .max_accel_m_s2 = 2.0f,
    .max_alpha_rad_s2 = 4.0f,
    .drive_speed_pid = { .kp = 13.0f * DJI_M3508_RPM_PER_RAD_S / DJI_M3508_RAW_PER_NM,
                         .ki = 0.1f * DJI_M3508_RPM_PER_RAD_S / DJI_M3508_RAW_PER_NM,
                         .integral_limit = 5000.0f / DJI_M3508_RPM_PER_RAD_S,
                         .output_limit = 12000.0f / DJI_M3508_RAW_PER_NM },
};

/* ------------------------------------------------------------------ */
/* 遥控                                                                */
/* ------------------------------------------------------------------ */

/*
 * 遥控 → 底盘速度（满杆时的速度）。第一版底盘直接读遥控（ADR 0043）：
 * 左摇杆上下 ch[3] → 前后，左摇杆左右 ch[2] → 左右，右摇杆左右 ch[0] → 旋转；
 * 通道对应沿用旧工程编号，待 V11 核对。首次上台架先用小值
 */
static const float max_vx_m_s = 1.0f;
static const float max_vy_m_s = 1.0f;
static const float max_wz_rad_s = 2.0f;

/* 解锁 / 急停拨杆：右拨杆 sw[1]（ADR 0032；左右以 V11 上板核对为准） */
static const uint8_t arm_switch = 1u;

/* ------------------------------------------------------------------ */
/* IMU                                                           */
/* ------------------------------------------------------------------ */

/*
 * IMU 安装旋转：机体系向量 = R × 芯片系向量（按行存储，ADR 0006）。
 * 默认芯片轴与机体轴同向（X 前、Y 左、Z 上）；实际朝向以 V7 上板结果为准
 */
static const InsConfig ins_config = { .install_rotation = {
                                          1.0f, 0.0f, 0.0f, /* 机体 x */
                                          0.0f, 1.0f, 0.0f, /* 机体 y */
                                          0.0f, 0.0f, 1.0f, /* 机体 z */
                                      } };
