/**
 * @file    supercap.h
 * @brief   超级电容控制板（cap-pcmc，STM32G474）的 CAN 通信：发指令、收反馈
 * @note    超级电容是什么：底盘功率被裁判系统限制（超了扣血），电容在底盘用电少时把多余的功率存起来，
 *          加速、爬坡时再放出来，让底盘短时间用上比限制更大的功率。电容板自己做充放电控制，
 *          主控只需要告诉它“功率上限是多少、要不要输出”，并读回它的状态。
 *
 *          协议是草案（版本 1），帧格式见同目录 README.md，电容板固件按它实现；两边改协议时同时改版本号。
 *          - 电容板是经典 CAN（1 Mbit/s），只能接在经典 CAN 总线上（和电机共用一路），不能接 CAN FD 总线；
 *          - 两个 ID 不能和同一路上的电机重复，supercap_init() 会检查；
 *          - 指令每 SUPERCAP_CMD_PERIOD_MS 发一次（control_task 每周期调用 supercap_send()，内部限频）；
 *          - 反馈用法同电机：comm_rx_task 收帧 → supercap_receive()；使用方 supercap_read()，离线返回 false。
 *
 *          一个电容怎么用：
 *            初始化  robot_init() 里电机都初始化之后 supercap_init()
 *            收      comm_rx_task  can_read() 取一帧 → supercap_receive()，是它的就存下、喂狗
 *            读      使用方任务  supercap_read()：拷贝一份反馈，当场判断在不在线
 *            发      control_task  每周期 supercap_send()，到时间才真的发
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "02_devices/motor/motor.h"
#include "04_core/watchdog/watchdog.h"
#include "05_platform/can/can.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 协议版本：指令和反馈的第 5、7 字节都带它，两边不一致时反馈帧不采用（见 README.md） */
#define SUPERCAP_PROTOCOL_VERSION 1u

/** 指令发送周期：电容板 100 ms 收不到指令就自己停止输出，10 ms 一帧留足余量 */
#define SUPERCAP_CMD_PERIOD_MS 10u

/** 反馈超过这么久收不到就算离线（电容板 1 ms 发一帧，50 ms 即连续丢 50 帧） */
#define SUPERCAP_TIMEOUT_MS 50u

/** 一块电容板的固定参数：写在 robot.h 里 */
typedef struct
{
    const char *name;     /* 日志和设备清单里的名字 */
    CanBusId can_bus;     /* 必须是经典 CAN 总线 */
    uint32_t cmd_id;      /* 主控 → 电容的指令帧 ID（11 位） */
    uint32_t feedback_id; /* 电容 → 主控的反馈帧 ID（11 位） */
} SupercapConfig;

/** 主控 → 电容：本周期的要求 */
typedef struct
{
    bool enable_output; /* true：允许电容板向底盘输出；false：电容板只让电池直接供电 */
    float
        power_limit_w; /* 底盘功率上限（W），电容板按它控制从电池取的功率；通常就是裁判系统给的上限 */
    float
        buffer_energy_j; /* 裁判系统的缓冲能量（J）：电容板据此判断还能多取多少功率；不知道时给 0 */
} SupercapCmd;

/** 电容 → 主控：最新一帧反馈，单位都已换算好 */
typedef struct
{
    uint8_t error_flags; /* 电容板报的错误，0 = 正常；各位含义见 README.md */
    bool output_enabled; /* 电容板此刻是否在向底盘输出 */
    float cap_voltage_v; /* 电容组电压 */
    float chassis_power_w; /* 电容板测到的底盘功率（W），可为负（电机回馈充电） */
    float energy_ratio; /* 电容剩余可用能量，0–1 */
} SupercapFeedback;

typedef struct
{
    const SupercapConfig *cfg;
    Watchdog wd; /* fb 的接收时刻和超时：supercap_read() 和 detect 日志都只看它 */
    SupercapFeedback fb;  /* 最新一帧合法反馈，只通过 supercap_read() 读 */
    uint64_t last_cmd_us; /* 上次发指令的时刻（限频用） */
    bool cmd_sent;        /* 是否发过指令（第一次立刻发） */
    uint32_t bad_frames;  /* ID 对但长度或版本不对、被丢弃的帧数（调试用） */
    uint32_t tx_dropped;  /* 发送队列满被丢弃的指令帧数（调试用） */
} Supercap;

typedef enum
{
    SUPERCAP_INIT_OK,
    SUPERCAP_INIT_BAD_CONFIG, /* ID 超过 11 位，或指令和反馈用了同一个 ID */
    SUPERCAP_INIT_FD_BUS, /* 总线是 CAN FD：经典 CAN 的电容板接上去会把总线搞乱 */
    SUPERCAP_INIT_MOTOR_CONFLICT, /* ID 被这路总线上的电机占用 */
} SupercapInitResult;

/**
 * @brief   初始化：检查配置和 ID 冲突，登记看门狗（出现在 detect_task 的设备清单里）
 * @param   motors  本板的电机组，查 ID 冲突用
 * @pre     初始化阶段、电机全部 motor_init() 之后调用；cfg 在整个运行期间有效；只初始化一次
 */
RM_NODISCARD SupercapInitResult supercap_init(Supercap *cap, const SupercapConfig *cfg,
                                              const MotorGroup *motors);

/**
 * @brief   把一帧 CAN 交给电容：总线和反馈 ID 都对上就是它的，解码、存下、喂狗
 * @return  true：这帧是它的（长度或版本不对的也算，丢弃不喂狗）；false：不是它的
 * @pre     只在 comm_rx_task 里调用
 */
bool supercap_receive(Supercap *cap, CanBusId bus, const CanFrame *frame);

/**
 * @brief   拷贝最新一份反馈
 * @return  false：离线（从未收到，或超过 SUPERCAP_TIMEOUT_MS 没有新反馈）；这时 out 是旧数据，只能用来打印
 */
RM_NODISCARD bool supercap_read(const Supercap *cap, SupercapFeedback *out);

/**
 * @brief   发指令：每个控制周期调用，距上次发送满 SUPERCAP_CMD_PERIOD_MS 才真的发一帧
 * @note    发送队列满时丢弃这一帧（计入 tx_dropped），下一个周期再发新的
 * @pre     只在 control_task 里调用
 */
void supercap_send(Supercap *cap, const SupercapCmd *cmd);

/* ---- 编解码（纯计算，电脑上可测；电容板固件可照着实现另一边） ---- */

/** 把指令编码成 8 字节：数值超出范围时截到边界，负数按 0 */
void supercap_encode_cmd(const SupercapCmd *cmd, uint8_t out[8]);

/**
 * @brief   把 8 字节反馈解码
 * @return  false：版本号不是 SUPERCAP_PROTOCOL_VERSION（两边协议不一致），out 不变
 */
RM_NODISCARD bool supercap_decode_feedback(const uint8_t data[8], SupercapFeedback *out);

#ifdef __cplusplus
}
#endif
