/**
 * @file    vt_link.h
 * @brief   图传链路：VT13 图传遥控器帧与 0xA5 帧（键鼠 0x0304）的分帧、校验、解析
 * @note    移植自 COD-H7-Template `Components/Device/Src/Image_Transmission.c`，字段位布局不变。与旧工程的差异：
 *          - 在 comm_rx_task 里按字节流解析：帧头 + CRC 找帧，一次能解析多帧、拆段能拼、错位能恢复
 *            （旧代码只看缓冲区开头的一帧）；
 *          - VT13 摇杆超出 364–1684 的帧丢弃（范围沿用 DR16，待上板核对）；
 *          - 0x0302 自定义控制器数据、0x0309 发送暂不移植（没有使用者；旧代码发送时没加帧头和 CRC）；
 *          - 串口改为 USART10（921600，ADR 0036）；旧工程与裁判系统共用 USART1、编译开关二选一。
 *          VT13 帧：0xA9 0x53 开头共 21 字节，末 2 字节是前 19 字节的 CRC16（小端）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/watchdog/watchdog.h"
#include "05_platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** 挡位开关（取值同 VT13 协议） */
typedef enum
{
    VT_MODE_C = 0,
    VT_MODE_N = 1,
    VT_MODE_S = 2,
} VtMode;

typedef struct
{
    int16_t ch[4]; /* 摇杆，已减中位 1024，约 ±660；各通道对应哪根摇杆待上板核对 */
    int16_t wheel;     /* 拨轮，已减中位 */
    VtMode mode;       /* 挡位开关 */
    bool pause;        /* 暂停（急停）键 */
    bool custom_left;  /* 自定义键：左 */
    bool custom_right; /* 自定义键：右 */
    bool trigger;      /* 扳机键 */
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    uint8_t mouse_left; /* 鼠标左、右、中键，各 2 位原始值 */
    uint8_t mouse_right;
    uint8_t mouse_middle;
    uint16_t keys; /* 位定义同 RcState 的 RC_KEY_* */
} VtRcState;

_Static_assert(sizeof(VtRcState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

typedef struct
{
    int16_t mouse_x;
    int16_t mouse_y;
    int16_t mouse_z;
    bool mouse_left;
    bool mouse_right;
    uint16_t keys; /* 位定义同 RcState 的 RC_KEY_* */
} KbmState;

_Static_assert(sizeof(KbmState) <= 256, "消息不超过 256 字节（运行时契约第 2 节）");

#define VT13_FRAME_LEN     21u
#define VT_LINK_BUF_LEN    64u /* 图传链路上最长的 0xA5 帧是 0x0302 的 39 字节 */
#define VT_LINK_TIMEOUT_MS 200u /* 超过这么久没收到就算掉线（vt_link_read_*() 返回 false） */

typedef struct
{
    VtRcState rc;    /* 最新一帧 VT13 遥控器数据，只通过 vt_link_read_rc() 读 */
    Watchdog rc_wd;  /* rc 的接收时刻和超时（设备清单里叫 vt13） */
    KbmState kbm;    /* 最新一帧键鼠数据，只通过 vt_link_read_kbm() 读 */
    Watchdog kbm_wd; /* kbm 的接收时刻和超时（设备清单里叫 vt_kbm） */
    uint8_t buf[VT_LINK_BUF_LEN];
    uint32_t len;
    uint32_t bad_bytes;      /* 找帧时丢弃的字节数（调试用） */
    uint32_t ignored_frames; /* 校验通过但不处理的命令（调试用） */
} VtLink;

/** 初始化、登记看门狗  @pre 初始化阶段调用 */
void vt_link_init(VtLink *self);

/** 读最新的 VT13 遥控器数据  @return false：超过 VT_LINK_TIMEOUT_MS 没收到（out 是旧数据） */
RM_NODISCARD bool vt_link_read_rc(const VtLink *self, VtRcState *out);

/** 读最新的键鼠数据  @return false：超过 VT_LINK_TIMEOUT_MS 没收到（out 是旧数据） */
RM_NODISCARD bool vt_link_read_kbm(const VtLink *self, KbmState *out);

/** 喂入从串口读到的一段字节（comm_rx_task 里调用） */
void vt_link_on_bytes(VtLink *self, const uint8_t *data, uint32_t len);

/** 解析一帧 VT13（纯计算） @return false：CRC 错或摇杆超范围 */
bool vt13_decode(const uint8_t frame[VT13_FRAME_LEN], VtRcState *out);

#ifdef __cplusplus
}
#endif
