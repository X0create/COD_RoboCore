/**
 * @file    bmi088.h
 * @brief   BMI088 六轴 IMU（加速度计 + 陀螺仪两颗芯片共用 SPI2）与加热恒温
 * @note    移植自 COD-H7-Template `Components/Device/Src/Bmi088.c` 和 `INS_Task.c` 的加热部分：
 *          寄存器配置、量程（±6 g、±2000 °/s）、换算系数、初始化顺序不变。与旧工程的差异：
 *          - 读写走 05_platform/spi 的 select / transfer / deselect，一个事务一次完成，驱动里没有引脚信息；
 *          - 加速度计读数在地址后多一个 dummy 字节、陀螺仪没有，写成显式参数（旧代码靠宏嵌套偶然对上，
 *            UniC `blocking-spi-is-deliberate`）；
 *          - 陀螺仪每次读从芯片 ID 寄存器开始，ID 不对就判这一帧无效（旧代码只是不更新）；
 *          - 初始化失败返回原因，不在驱动里无限重试；
 *          - 零偏由调用者设置（第 8 步上电标定，ADR 0033），默认 0；旧工程用写死的常数；
 *          - 加热输出为负时占空比为 0（旧代码负数直接转 uint16_t，变成满占空比加热）；
 *          - 加热参数改为 UniC 在同款 MC02 上实测的值（25% 上限、打开积分、每 100 ms 一次，见 bmi088.c）。
 *          恒温控制在设备内部完成（《架构设计》“其他设备”）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "03_algorithm/control/pid.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 加热目标温度 */
#define BMI088_HEATER_TARGET_C 40.0f

typedef struct
{
    float gyro_rad_s[3]; /* 芯片坐标系，已减零偏 */
    float accel_m_s2[3]; /* 芯片坐标系（换算系数按 g = 9.8 m/s²，同旧工程） */
    float temperature_c;
} Bmi088Sample;

typedef enum
{
    BMI088_OK,
    BMI088_BUS_BUSY,            /* SPI 总线被占用 */
    BMI088_ACCEL_NOT_FOUND,     /* 加速度计芯片 ID 不对 */
    BMI088_GYRO_NOT_FOUND,      /* 陀螺仪芯片 ID 不对 */
    BMI088_ACCEL_CONFIG_FAILED, /* 加速度计寄存器写入后读回不一致 */
    BMI088_GYRO_CONFIG_FAILED,  /* 陀螺仪寄存器写入后读回不一致 */
    BMI088_HEATER_START_FAILED, /* 加热 PWM 启动失败 */
} Bmi088Status;

typedef struct
{
    float gyro_offset_rad_s[3];
    Pid heater_pid;
    uint32_t heater_tick; /* 加热 PID 分频计数 */
} Bmi088;

/**
 * @brief   软复位两颗芯片、写入配置并读回核对，启动加热 PWM（占空比 0）
 * @note    阻塞约 170 ms（两次复位各等 80 ms），在任务里调用
 */
Bmi088Status bmi088_init(Bmi088 *imu);

/**
 * @brief   读一次加速度、温度、角速度
 * @return  false：总线被占用或陀螺仪 ID 不对，这一帧无效，out 不可用
 */
bool bmi088_read(Bmi088 *imu, Bmi088Sample *out);

void bmi088_set_gyro_offset(Bmi088 *imu, const float offset_rad_s[3]);

/**
 * @brief   加热恒温，每次读到新温度时调用（1 kHz）；内部每 HEATER_PERIOD_MS（1280）次算一次 PID（ADR 0042）
 */
void bmi088_heater_step(Bmi088 *imu, float temperature_c);

/**
 * @brief   关闭加热（读不到温度时调用）
 * @note    不清 PID：偶尔一次读失败后，下一次读到温度就从原来的积分接着控制。
 *          清掉的话，稳态约 16% 占空比全靠积分维持，要重新攒几十秒，温度会往下掉（2026-09-30 改）
 */
void bmi088_heater_off(Bmi088 *imu);

#ifdef __cplusplus
}
#endif
