/**
 * @file    board_link.h
 * @brief   板间通信：一条跨板消息占一个 CAN ID，帧头 2 字节（序号、数据年龄）+ 载荷（《架构设计》运行时契约第 8 节）
 * @note    COD-H7-Template 没有板间通信，本模块为新写；帧格式按《架构设计》第 8 节，
 *          字段逐个按小端写进载荷（不 memcpy 结构体，两块板的编译器、填充字节可能不同）。
 *
 *          帧格式（data[] 下标）：
 *            [0]    seq     每条消息各自递增，接收方据此统计丢帧
 *            [1]    age_ms  数据在发送时已经多老：0–254 ms；255 = 未知或已过期，接收方不采用
 *            [2..]  载荷    payload_len 字节，内容由编码表规定（见本目录 README.md）
 *          载荷 ≤ 6 字节发经典帧（任何总线都能用）；7–62 字节发 CAN FD 帧，只能用在全 FD 的总线上（ADR 0023）。
 *          第一版不做多帧拼接：装不下就拆成几条各自完整的消息。
 *
 *          一条消息怎么用（和电机“收 → 读 → 发”相同）：
 *            两块板用同一张配置表（同一个 BoardLinkConfig），一块按 BOARD_LINK_TX 初始化，另一块按 BOARD_LINK_RX；
 *            发：发送方任务里按编码表填好载荷 → board_link_send()；
 *            收：comm_rx_task  can_read() 取一帧 → board_link_receive()，是它的就存下、喂狗；
 *            读：使用方任务里 board_link_read()，超时（含数据年龄）返回 false → 依赖它的机构执行机构停。
 *          接收时刻按“本地收帧时刻 − age_ms”记，所以对方数据本身的年龄也算进超时（运行时契约第 8 节）。
 *
 *          尚未实现（阶段 8 再做，见 README.md）：心跳里的 boot_id、协议版本、两块板的解锁互锁。
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

#define BOARD_LINK_HEADER_LEN  2u
#define BOARD_LINK_MAX_CLASSIC 6u  /* 经典帧 8 字节 − 帧头 */
#define BOARD_LINK_MAX_PAYLOAD 62u /* FD 帧 64 字节 − 帧头 */
#define BOARD_LINK_AGE_UNKNOWN 255u
#define BOARD_LINK_I16_INVALID INT16_MIN /* board_link_float_to_i16() 对 NaN 的编码 */

/** 一条跨板消息的固定参数：写在编码表里，两块板共用同一份 */
typedef struct
{
    const char *name; /* 日志和设备清单里的名字 */
    CanBusId can_bus;
    uint32_t id; /* 11 位标准 ID，不能和这路总线上的电机、其他跨板消息重复 */
    uint8_t payload_len; /* 1–62；> 6 时发 FD 帧 */
    uint32_t timeout_ms; /* 接收方：超过这么久（含数据年龄）没有新数据就算离线 */
} BoardLinkConfig;

typedef enum
{
    BOARD_LINK_TX, /* 本板发送 */
    BOARD_LINK_RX, /* 本板接收：登记看门狗，出现在设备清单和上线 / 离线日志里 */
} BoardLinkDir;

typedef struct BoardLink
{
    const BoardLinkConfig *cfg;
    BoardLinkDir dir;
    Watchdog wd;                             /* 接收方：payload 的接收时刻和超时 */
    uint8_t payload[BOARD_LINK_MAX_PAYLOAD]; /* 接收方：最新一份载荷，只通过 board_link_read() 读 */
    uint8_t tx_seq;                          /* 发送方：下一帧的序号 */
    uint8_t last_rx_seq;                     /* 接收方：上一帧的序号 */
    bool have_rx_seq;
    uint32_t lost; /* 接收方：按序号推算丢掉的帧数（调试用，阶段 8 据此决定是否开自动重发） */
    uint32_t bad_frames; /* 接收方：ID 对但长度不对、被丢弃的帧数（调试用） */
    struct BoardLink *next; /* 已初始化的消息链表，查 ID 冲突用 */
} BoardLink;

typedef enum
{
    BOARD_LINK_INIT_OK,
    BOARD_LINK_INIT_BAD_CONFIG,     /* ID 超过 11 位、载荷长度为 0 或超过 62 */
    BOARD_LINK_INIT_NEEDS_FD,       /* 载荷 > 6 字节，但这路总线不是 FD */
    BOARD_LINK_INIT_MOTOR_CONFLICT, /* ID 被这路总线上的电机占用 */
    BOARD_LINK_INIT_LINK_CONFLICT,  /* ID 被另一条跨板消息占用 */
} BoardLinkInitResult;

/**
 * @brief   初始化一条消息：检查配置和 ID 冲突；接收方登记看门狗
 * @param   motors  本板的电机组，查 ID 冲突用（两块板各查各的电机，总线上的每个电机都会被其中一块查到）
 * @pre     初始化阶段、电机全部 motor_init() 之后调用；cfg 在整个运行期间有效；同一个 link 只初始化一次
 */
RM_NODISCARD BoardLinkInitResult board_link_init(BoardLink *link, const BoardLinkConfig *cfg,
                                                 BoardLinkDir dir, const MotorGroup *motors);

/**
 * @brief   发一帧：序号 +1，写帧头和载荷，放进 CAN 发送队列
 * @param   payload  cfg->payload_len 字节，已按编码表编好
 * @param   age_ms   这份数据在此刻已经多老（如传感器采样到现在的时间）；≥ 255 按“未知或已过期”发
 * @return  false：发送队列满，这一帧被丢弃（周期消息下个周期发新的，不补发）
 * @pre     link 按 BOARD_LINK_TX 初始化
 */
RM_NODISCARD bool board_link_send(BoardLink *link, const uint8_t *payload, uint32_t age_ms);

/**
 * @brief   把一帧 CAN 交给这条消息：总线和 ID 都对上就是它的
 * @return  true：这帧是它的（长度不对的也算，丢弃不喂狗；age_ms = 255 的不喂狗），调用方不用再交给别人；
 *          false：不是它的
 * @pre     link 按 BOARD_LINK_RX 初始化；只在 comm_rx_task 里调用
 */
bool board_link_receive(BoardLink *link, CanBusId bus, const CanFrame *frame);

/**
 * @brief   拷贝最新一份载荷（cfg->payload_len 字节，在临界区里整份拷贝）
 * @return  false：离线（从未收到，或超过 timeout_ms 没有新数据）；这时 out 是旧数据，只能用来打印
 */
RM_NODISCARD bool board_link_read(const BoardLink *link, uint8_t *out);

/* ---- 编码表用的小工具：统一小端 ---- */

void board_link_put_u16(uint8_t *p, uint16_t v);
uint16_t board_link_get_u16(const uint8_t *p);
void board_link_put_i16(uint8_t *p, int16_t v);
int16_t board_link_get_i16(const uint8_t *p);

/**
 * @brief   浮点按分辨率换成 int16：value / lsb 四舍五入，超出 ±32767 就截到边界（不回绕），NaN 编成 BOARD_LINK_I16_INVALID
 * @param   lsb  分辨率，> 0，如 0.001f 表示 1 = 0.001 m/s
 */
int16_t board_link_float_to_i16(float value, float lsb);

/**
 * @brief   board_link_float_to_i16() 的反变换
 * @return  false：raw 是 BOARD_LINK_I16_INVALID（发送方的值是 NaN），out 不变
 */
RM_NODISCARD bool board_link_i16_to_float(int16_t raw, float lsb, float *out);

#ifdef __cplusplus
}
#endif
