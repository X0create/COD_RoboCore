/**
 * @file    sysid.h
 * @brief   系统辨识插件：车上固件加这一对文件（sysid.c / sysid.h），写两张表、加几行调用即可
 *
 * 能做什么：
 * - 记录：每个控制周期把车上的一组量（关节角度、速度、力矩、指令……）连同时间戳发给电脑；
 * - 激励：电脑下命令，让一个轴做扫动、扫频或阶跃，测摩擦、重力、惯量、响应。
 * 电脑端：tools/sysid_log/sysid_log.py（记录、发命令）、tools/program/robot_window.py（界面）。
 *
 * 接入步骤（例子见 docs/cn/firmware_module.md）：
 * 1. 通道表：要记的量，每个一行，指向车上已有的变量（SYSID_F32 / SYSID_U32）；
 * 2. 轴表：哪些轴能被激励，各自的安全上限，指向该轴的实测位置和“收到反馈的次数”；
 * 3. 一个发送函数：把一帧字节交给 USB / 串口，忙时返回 false（下次再发同一帧）；
 * 4. 调用：
 *      sysid_init(&config);                  上电时一次
 *      sysid_receive(bytes, length);         收到电脑数据时（可以在中断里）
 *      sysid_poll();                         通信任务里每 1–2 ms 一次：解析收到的命令、发出记录
 *      sysid_control(dt);                    控制任务每周期，算指令之前：推进激励
 *      sysid_position(axis, &q, &w) 或
 *      sysid_offset(axis, target)            算指令时：有激励就改写 / 叠加这个轴的目标
 *      sysid_option()                        需要时：电脑随命令发来的车的选项（如“加前馈”）
 *      sysid_record(time_ticks);             控制任务每周期，指令算完之后：记一条
 *      sysid_stop();                         急停、上锁时
 *    只记录、不做激励的车，轴表可以为空，也不用调用 sysid_control / sysid_position / sysid_offset。
 *
 * 线程：sysid_receive 可在任何地方（包括中断）调用，只拷贝字节；sysid_poll 在通信任务；其余在控制任务。
 * 同一台车只有一个插件实例。C11，不依赖 HAL / RTOS。
 *
 * 安全（每一项对应一个具体故障）：
 * - 开始前检查参数：幅值、速度、加速度、频率不超过该轴的上限（位置扫频按峰值速度、加速度的上界查；
 *   位置目标上的阶跃速度无穷大，不接受），位置目标的整条轨迹落在允许窗口内——防电脑端参数写错把轴打到机械限位
 *   或让它猛跳；
 * - 速度、力矩目标运行中实测位置离开窗口就停——防轴慢慢漂到限位；
 * - 扫动时实测位置离轨迹太远就停——防轴被挡住；
 * - 该轴 feedback_timeout_s 内没有新反馈就停——防反馈停住后按旧角度继续激励；
 * - 车上急停、上锁时调用 sysid_stop()。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------------------------------------------------------------- 可在编译选项里改的大小 */

/** 一帧数据部分的最大长度，字节。一条记录 = 32 + 4 × 通道数，必须装进一帧（480 字节时最多 112 个通道）。 */
#ifndef SYSID_FRAME_DATA_MAX
#define SYSID_FRAME_DATA_MAX 480U
#endif

/** 待发记录的缓冲条数。通信偶尔卡住多久不丢数据 = 条数 × 控制周期。约占 条数 × SYSID_FRAME_DATA_MAX 字节。 */
#ifndef SYSID_LOG_RING_SIZE
#define SYSID_LOG_RING_SIZE 16U
#endif

/** 收到的字节在被 sysid_poll() 处理前的缓冲大小。电脑发的命令每条不到 50 字节。 */
#ifndef SYSID_RX_RING_SIZE
#define SYSID_RX_RING_SIZE 256U
#endif

/** 轴表最多几项。 */
#ifndef SYSID_MAX_AXES
#define SYSID_MAX_AXES 8U
#endif

/* ---------------------------------------------------------------- 车的配置 */

/** 通道的数值类型。 */
typedef enum
{
    SYSID_VALUE_F32 = 0, /**< float，SI 单位 */
    SYSID_VALUE_U32 = 1, /**< uint32_t：时间戳（与记录时间同一时钟）、计数、状态码 */
} SysIdValueType;

/** 一个通道：每次 sysid_record() 时读 source 指向的变量。 */
typedef struct
{
    const char *name; /**< 如 "j1.q"、"imu.gz"；ASCII，≤ 31 字节 */
    const char *unit; /**< 如 "rad"、"N.m"、"tick"；ASCII，≤ 15 字节 */
    SysIdValueType type;
    const volatile void *source; /**< 车上的变量：float（F32）或 uint32_t（U32） */
} SysIdChannel;

/** 写通道表用：SYSID_F32("j1.q", "rad", &motor[0].position) */
#define SYSID_F32(name, unit, variable)                                                            \
    {                                                                                              \
        (name), (unit), SYSID_VALUE_F32, (variable)                                                \
    }
#define SYSID_U32(name, unit, variable)                                                            \
    {                                                                                              \
        (name), (unit), SYSID_VALUE_U32, (variable)                                                \
    }

/** 激励作用的目标。 */
typedef enum
{
    SYSID_TARGET_POSITION = 0, /**< 轴按轨迹走：目标位置 = 起始位置 + offset，速度前馈 = rate */
    SYSID_TARGET_VELOCITY = 1, /**< 在平时的速度目标上叠加 offset */
    SYSID_TARGET_TORQUE = 2,   /**< 在平时的力矩（或电流）指令上叠加 offset */
    SYSID_TARGET_COUNT = 3,
} SysIdTarget;

/**
 * 一个可激励的轴（各量的单位跟该轴的位置单位走：转动轴 rad，直线轴 m）。
 * max_offset[目标] 为 0 表示该轴不接受这种目标。有窗口的轴，扫动的偏移只受窗口限制。
 */
typedef struct
{
    float max_offset
        [SYSID_TARGET_COUNT]; /**< 幅值上限：位置 rad、速度 rad/s、力矩 N·m（或该轴指令的单位） */
    bool has_window; /**< 是否检查位置窗口（无限转的轴、底盘、摩擦轮为 false） */
    float window_min; /**< 实测位置的允许范围；按机械限位往里留余量 */
    float window_max;
    float max_speed; /**< 扫动速度、位置扫频峰值速度的上限，rad/s */
    float max_accel; /**< 扫动加速度、位置扫频峰值加速度的上限，rad/s² */
    float max_tracking_error;       /**< 扫动时实测位置离轨迹的上限；0 = 不检查 */
    const volatile float *position; /**< 该轴的实测位置 */
    const volatile uint32_t *feedback_count; /**< 收到反馈的次数（每收到一帧加 1）；NULL = 不检查 */
    float feedback_timeout_s; /**< 这么久计数没变就算反馈丢失，激励停止 */
} SysIdAxisConfig;

/**
 * 发出一帧。上一帧还没发完时返回 false，插件下次再发同一帧。
 * 一次返回 true 的调用交出的 frame，到下一次返回 true 的调用之前不会被改写（插件轮流用两块缓冲区），
 * 所以直接交给不拷贝数据的 USB 发送（如 CDC_Transmit_HS）也可以。
 */
typedef bool (*SysIdSendFunction)(const uint8_t *frame, uint16_t length);

/** 插件配置，须一直有效（通常是 static const）。 */
typedef struct
{
    const char *source;           /**< 来源名，如 "custom_control arm"；≤ 31 字节 */
    const SysIdChannel *channels; /**< 通道表 */
    uint8_t channel_count;
    const SysIdAxisConfig *axes; /**< 轴表；不做激励时为 NULL */
    uint8_t axis_count;
    float period_s; /**< 控制周期，s；扫频最高频率不超过它的 1/4 */
    uint32_t clock_hz; /**< sysid_record() 时间戳的时钟频率，Hz（DWT 周期计数时就是内核频率） */
    SysIdSendFunction send;
} SysIdConfig;

/* ---------------------------------------------------------------- 调用 */

/**
 * @brief  初始化，并安排先发通道表。
 * @return false：配置不合理（通道或轴表为空 / 太多、名称太长、一条记录装不进一帧、上限为负、窗口颠倒……）；
 *         开发时就会发现，此后其余函数什么也不做。
 */
bool sysid_init(const SysIdConfig *config);

/** @brief 收到电脑发来的字节（USB / 串口接收回调里调用，可以在中断里）。别的协议的帧也可以一起给，会被忽略。 */
void sysid_receive(const uint8_t *data, uint32_t length);

/** @brief 通信任务里每 1–2 ms 调用一次：处理收到的命令，把待发的通道表和记录交给 send。 */
void sysid_poll(void);

/**
 * @brief  控制任务每周期调用一次，在计算本周期指令之前：开始 / 推进 / 停止激励。
 * @param  dt_s  距上次调用的时间，s。
 */
void sysid_control(float dt_s);

/** @brief 本周期是否有激励在进行（有的车激励时要换控制方式，如机械臂其余关节锁在原位）。 */
bool sysid_active(void);

/**
 * @brief  位置目标：这个轴正在被激励时，把 *position 改成轨迹位置、*velocity 改成速度前馈，返回 true；
 *         否则什么也不改，返回 false。
 */
bool sysid_position(uint8_t axis, float *position, float *velocity);

/** @brief 速度或力矩目标：这个轴正在被这种目标激励时返回要叠加的量，否则返回 0。 */
float sysid_offset(uint8_t axis, SysIdTarget target);

/**
 * @brief  电脑随本次激励命令发来的“车的选项”（0–255），没有激励时为 0。插件不解释它，含义由车定
 *         （如机械臂：1 = 被激励的关节加摩擦前馈，用来对比加与不加的跟踪）。
 */
uint8_t sysid_option(void);

/**
 * @brief  记一条：读通道表里的全部变量，排队等 sysid_poll() 发出。控制任务每周期调用一次，在指令算完之后。
 * @param  time_ticks  本条的时刻，clock_hz 时钟的计数（允许回绕）。
 */
void sysid_record(uint32_t time_ticks);

/** @brief 车上要求停止激励（急停、上锁、遥控离线等），在控制任务里调用。 */
void sysid_stop(void);

#ifdef __cplusplus
}
#endif
