/**
 * @file    vt_link.h
 * @brief   图传链路：VT13 图传遥控器帧与 0xA5 帧（键鼠 0x0304）的分帧、校验、解析
 * @note    移植自 COD-H7-Template `Components/Device/Src/Image_Transmission.c`，字段位布局不变。与旧工程的差异：
 *          - 在 comm_rx 任务里按字节流解析：帧头 + CRC 找帧，一次能解析多帧、拆段能拼、错位能恢复
 *            （旧代码只看缓冲区开头的一帧）；
 *          - VT13 摇杆超出 364–1684 的帧丢弃（范围沿用 DR16，待上板核对）；
 *          - 0x0302 自定义控制器数据、0x0309 发送暂不移植（没有使用者；旧代码发送时没加帧头和 CRC）；
 *          - 串口改为 USART10（921600，ADR 0036）；旧工程与裁判系统共用 USART1、编译开关二选一。
 *          VT13 帧：0xA9 0x53 开头共 21 字节，末 2 字节是前 19 字节的 CRC16（小端）。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/msg/kbm_state.h"
#include "core/msg/vt_rc_state.h"
#include "core/watchdog/watchdog.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define VT13_FRAME_LEN     21u
#define VT_LINK_BUF_LEN    64u  /* 图传链路上最长的 0xA5 帧是 0x0302 的 39 字节 */
#define VT_LINK_TIMEOUT_MS 200u /* 只用于上线 / 离线日志 */

typedef struct
{
    VtRcStateTopic *rc_out;
    KbmStateTopic *kbm_out;
    Watchdog wd;
    uint8_t buf[VT_LINK_BUF_LEN];
    uint32_t len;
    uint32_t bad_bytes;      /* 找帧时丢弃的字节数（调试用） */
    uint32_t ignored_frames; /* 校验通过但不处理的命令（调试用） */
} VtLink;

/** @return false：话题已被认领  @pre 初始化阶段调用 */
RM_NODISCARD bool vt_link_init(VtLink *self, VtRcStateTopic *rc_out, KbmStateTopic *kbm_out);

/** 喂入从串口读到的一段字节（comm_rx 任务里调用） */
void vt_link_on_bytes(VtLink *self, const uint8_t *data, uint32_t len);

/** 解析一帧 VT13（纯计算） @return false：CRC 错或摇杆超范围 */
bool vt13_decode(const uint8_t frame[VT13_FRAME_LEN], VtRcState *out);

#ifdef __cplusplus
}
#endif
