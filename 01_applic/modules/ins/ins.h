/**
 * @file    ins.h
 * @brief   惯性导航子系统：读 BMI088 → 上电零偏标定 → 安装旋转 → 加速度低通 → 四元数 EKF → 保存最新 ImuState（ins_read() 读）
 * @note    名词：
 *            零偏    陀螺仪静止时读数也不是 0，这个固定的偏差叫零偏；不减掉的话，积分出的角度会一直漂
 *            EKF     扩展卡尔曼滤波：用陀螺仪积分算姿态（快但会漂），再用加速度计测到的重力方向去修正（慢但不漂）
 *            四元数  表示三维旋转的 4 个数，没有欧拉角“万向节死锁”的问题，控制计算都用它
 *          流程（每 1 ms 调用一次 ins_step()）：
 *          - 上电静止标定陀螺零偏：采 INS_CALIB_SAMPLES 个样本，标准差和均值都在阈值内才采用，
 *            否则报告原因（车在动 / 零偏过大）并重新采样；标定完成前不保存 ImuState，ins_read() 返回 false，
 *            用到姿态的机构此时不应动作；
 *          - 芯片坐标系用 robot.h 配置的安装旋转转到机体系，板子怎么装都只改那个矩阵；
 *          - 加速度模长接近 0 的读数当作坏帧丢弃（全零会让 EKF 除零后永久变成 NaN）；
 *          - 读失败时关加热、不更新姿态；
 *          - EKF 用实测的两次更新间隔，任务偶尔被耽误时积分时间仍然准确；
 *          - 运行中静止时在线修正航向轴零偏（见下方 INS_STILL_*）。
 *          本模块不打日志：ins_step() 返回事件，由调用它的任务记录。
 *          目前是轮询：每 1 ms 读一次 BMI088；以后可以改成用 BMI088 的“数据就绪”中断触发，采样时刻更准。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "02_devices/imu/bmi088.h"
#include "03_algorithm/attitude/gyro_bias.h"
#include "03_algorithm/attitude/quat_ekf.h"
#include "03_algorithm/filter/lpf.h"
#include "04_core/watchdog/watchdog.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 上电标定（实测：BMI088 静止时陀螺噪声约 0.013 rad/s） */
#define INS_CALIB_SAMPLES  2000u /* 1 kHz 下 2 s */
#define INS_CALIB_MAX_STD  0.05f /* rad/s，约 3.8 倍静止噪声；碰一下（≥ 0.1 rad/s）就会拒绝 */
#define INS_CALIB_MAX_BIAS 0.15f /* rad/s，BMI088 零偏远小于 0.1；更大说明在匀速转 */

/*
 * 运行中在线修正航向轴零偏。零偏随芯片温度变化：上电标定时加热刚开始，升到 40 °C 后零偏就不准了
 * （实测：冷态标定后静止航向漂约 0.67 °/s，热态标定约 0.006 °/s）。x、y 轴零偏 EKF 已用重力估计，
 * 航向轴不可观测，由这里补：每 1 s 一个窗口，机体 z 轴角速度的标准差和均值都小于阈值就判为静止，
 * 把均值的 INS_STILL_GAIN 倍并入零偏。代价：比 INS_STILL_MAX_RATE 慢、又很平稳的真实转动会被部分当成零偏。
 */
#define INS_STILL_SAMPLES  1000u /* 1 kHz 下 1 s */
#define INS_STILL_MAX_STD  0.03f /* rad/s，静止噪声约 0.013 */
#define INS_STILL_MAX_RATE 0.02f /* rad/s（约 1.1 °/s），大于冷热零偏差 0.012；均值更大当作在转 */
#define INS_STILL_GAIN     0.1f /* 每个静止窗口修掉残余的 10%，时间常数约 10 s */

/** 超过这么久没有新姿态就算 IMU 未就绪（ins 1 kHz，20 ms 即连续 20 次没有更新） */
#define INS_TIMEOUT_MS 20u

typedef struct
{
    float q[4]; /* 机体系 → 世界系，[w x y z]，已归一化 */
    /* ZYX 欧拉角，只用于显示和调试，控制用四元数。机体系 X 朝前、Y 朝左、Z 朝上。
     * 都是绕对应轴右手为正：yaw 从上往下看逆时针为正；pitch 绕 +Y（朝左）为正，即**低头为正**，
     * 与云台常用的“抬头为正”相反，写云台时注意取反；roll 绕 +X（朝前）为正，即左侧抬起为正 */
    float yaw_rad;
    float pitch_rad;
    float roll_rad;
    float yaw_total_rad; /* 多圈航向（不回绕） */
    float gyro_rad_s[3]; /* 机体系角速度，已减上电标定的零偏 */
    float accel_m_s2[3]; /* 机体系加速度，二阶低通后 */
    float temperature_c; /* IMU 芯片温度 */
} ImuState;

_Static_assert(sizeof(ImuState) <= 256,
               "ImuState 不超过 256 字节：读取时整块拷贝要关中断，太大会拖长关中断时间");

typedef struct
{
    /* 安装旋转：机体系向量 = R × 芯片系向量，按行存储；芯片与机体同向时为单位阵 */
    float install_rotation[9];
} InsConfig;

typedef enum
{
    INS_PHASE_CALIBRATING,
    INS_PHASE_RUNNING,
} InsPhase;

typedef enum
{
    INS_EVENT_NONE,
    INS_EVENT_READ_FAILED,          /* 这次没读到有效数据（加热已关） */
    INS_EVENT_CALIBRATED,           /* 标定完成，开始保存姿态 */
    INS_EVENT_CALIB_NOT_STILL,      /* 标定被拒：在动，重新采样 */
    INS_EVENT_CALIB_BIAS_TOO_LARGE, /* 标定被拒：均值过大，重新采样 */
} InsEvent;

typedef struct
{
    const InsConfig *cfg;
    ImuState state; /* 最新姿态（标定完成后才写入），只通过 ins_read() 读 */
    Watchdog wd; /* state 的写入时刻和超时：ins_read() 的就绪判定和 detect 日志都只看它 */
    Bmi088 imu;
    InsPhase phase;
    GyroBias calib;
    GyroBias still; /* 运行中的静止检测窗口（机体系角速度） */
    QuatEkf ekf;
    Lpf2 accel_lpf[3];
    bool have_last_update;
    uint64_t last_update_us; /* 上次 EKF 更新的时刻 */
    bool have_yaw;
    float last_yaw_rad;
    int32_t yaw_turns;
    uint32_t read_failures; /* 调试用 */
} Ins;

/**
 * @brief   初始化滤波器和 EKF，登记看门狗
 * @pre     初始化阶段调用；同一个 ins 只初始化一次（看门狗只能登记一次）
 */
void ins_init(Ins *ins, const InsConfig *cfg);

/**
 * @brief   读最新姿态（在临界区里整份拷贝）
 * @return  false：IMU 未就绪（还没标定完，或超过 INS_TIMEOUT_MS 没有更新）；这时 out 是旧数据，只能用来打印
 */
RM_NODISCARD bool ins_read(const Ins *ins, ImuState *out);

/** 每 1 ms 调用一次 @pre bmi088_init(&ins->imu) 已返回 BMI088_OK（ins_task 开头调用） */
InsEvent ins_step(Ins *ins);

#ifdef __cplusplus
}
#endif
