/**
 * @file    dr16.h
 * @brief   DR16 遥控接收机（DBUS）：分帧、校验、解析，保存最新一帧 RcState，dr16_read() 读取
 * @note    移植自 COD-H7-Template `Components/Device/Src/Remote_Control.c`，字段解析的位运算不变。与旧工程的差异：
 *          - 在 comm_rx_task 里解析，不在串口中断里（运行时契约第 2 节）；
 *          - 按时间间隔分帧：距上次收到数据超过 DR16_FRAME_GAP_US 就从新帧开始，
 *            一帧被 DMA 半满中断切成几段也能拼起来（旧工程只接受一次空闲中断正好 18 字节）；
 *          - 摇杆 0–3 必须在 364–1684、两个拨杆必须是 1–3，否则整帧丢弃、不发布、不喂狗（附录 A.4）；
 *          - 丢失判定：dr16_read() 读最新一帧，200 ms 没有合法帧就返回 false（ADR 0030）；旧工程丢失时把数据清零，这里不更新即可。
 *          串口参数（100 kbit/s、偶校验、电平反相）由 CubeMX 配置 UART5，见附录 A.4。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/util/snapshot.h"
#include "04_core/watchdog/watchdog.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 超过这么久没有合法帧就算遥控丢失（ADR 0030：用户 2026-09-28 决定沿用旧工程的 200 ms） */
#define RC_LOST_TIMEOUT_MS 200u

/** 摇杆减去中位后的最大幅度（原始值 364–1684，中位 1024） */
#define RC_CH_MAX 660

/** 拨杆位置，取值与 DR16 协议一致 */
typedef enum
{
    RC_SW_UP = 1,
    RC_SW_DOWN = 2,
    RC_SW_MID = 3,
} RcSwitch;

/** 键盘位掩码 keys 的各位 */
enum
{
    RC_KEY_W = 1u << 0,
    RC_KEY_S = 1u << 1,
    RC_KEY_A = 1u << 2,
    RC_KEY_D = 1u << 3,
    RC_KEY_SHIFT = 1u << 4,
    RC_KEY_CTRL = 1u << 5,
    RC_KEY_Q = 1u << 6,
    RC_KEY_E = 1u << 7,
    RC_KEY_R = 1u << 8,
    RC_KEY_F = 1u << 9,
    RC_KEY_G = 1u << 10,
    RC_KEY_Z = 1u << 11,
    RC_KEY_X = 1u << 12,
    RC_KEY_C = 1u << 13,
    RC_KEY_V = 1u << 14,
    RC_KEY_B = 1u << 15,
};

typedef struct
{
    /* 0–3 为摇杆、4 为拨轮，已减去中位 1024。0–3 保证在 ±RC_CH_MAX 内；拨轮不做范围检查（见 dr16.c）。
     * 各通道对应哪根摇杆的哪个方向沿用旧工程的编号，待上板核对（VERIFICATION_TODO） */
    int16_t ch[5];
    RcSwitch sw[2]; /* [0] 左、[1] 右：沿用旧工程的注释，待上板核对 */
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    bool mouse_left;
    bool mouse_right;
    uint16_t keys; /* RC_KEY_* 位掩码 */
} RcState;

_Static_assert(sizeof(RcState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

#define DR16_FRAME_LEN 18u
/* DR16 每 14 ms 发一帧、一帧约 2 ms 传完，帧间空闲约 12 ms；超过 6 ms 没数据就认为下一字节是新帧开头（taproot 同值） */
#define DR16_FRAME_GAP_US 6000u

typedef struct
{
    RcState rc;    /* 最新一帧合法数据，只通过 dr16_read() 读 */
    Snapshot snap; /* rc 的写入时刻 */
    Watchdog wd;   /* 只用于上线 / 离线日志和设备清单；丢失判定看 dr16_read() */
    uint8_t frame[DR16_FRAME_LEN];
    uint8_t len;    /* frame 里已收到的字节数 */
    bool have_last; /* last_rx_us 是否有效 */
    uint64_t last_rx_us;
    uint32_t bad_frames; /* 校验不通过被丢弃的帧数（调试用） */
} Dr16;

/** 初始化、登记看门狗  @pre 初始化阶段调用 */
void dr16_init(Dr16 *self);

/**
 * @brief   读最新一帧遥控数据（在临界区里整份拷贝）
 * @return  false：遥控丢失（从未收到，或 RC_LOST_TIMEOUT_MS 内没有合法帧），out 不被修改
 */
RM_NODISCARD bool dr16_read(const Dr16 *self, RcState *out);

/**
 * @brief   喂入从串口读到的一段字节（comm_rx_task 里调用）
 * @param   now_us  读到这段字节的时刻（rm_time_now_us()），用来按时间间隔分帧
 */
void dr16_on_bytes(Dr16 *self, const uint8_t *data, uint32_t len, uint64_t now_us);

/**
 * @brief   解析并校验一帧（纯计算）
 * @return  false：摇杆或拨杆取值超出范围，out 内容无意义
 */
bool dr16_decode(const uint8_t frame[DR16_FRAME_LEN], RcState *out);

#ifdef __cplusplus
}
#endif
