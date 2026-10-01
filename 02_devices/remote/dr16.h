/**
 * @file    dr16.h
 * @brief   DR16 遥控接收机（DBUS）：分帧、校验、解析，发布 RcState
 * @note    移植自 COD-H7-Template `Components/Device/Src/Remote_Control.c`，字段解析的位运算不变。与旧工程的差异：
 *          - 在 comm_rx_task 里解析，不在串口中断里（运行时契约第 2 节）；
 *          - 按时间间隔分帧：距上次收到数据超过 DR16_FRAME_GAP_US 就从新帧开始，
 *            一帧被 DMA 半满中断切成几段也能拼起来（旧工程只接受一次空闲中断正好 18 字节）；
 *          - 摇杆 0–3 必须在 364–1684、两个拨杆必须是 1–3，否则整帧丢弃、不发布、不喂狗（附录 A.4）；
 *          - 丢失判定见 rc_state.h（话题新旧，ADR 0030）；旧工程丢失时把数据清零，这里不发布即可。
 *          串口参数（100 kbit/s、偶校验、电平反相）由 CubeMX 配置 UART5，见附录 A.4。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "04_core/msg/rc_state.h"
#include "04_core/watchdog/watchdog.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DR16_FRAME_LEN 18u
/* DR16 每 14 ms 发一帧、一帧约 2 ms 传完，帧间空闲约 12 ms；超过 6 ms 没数据就认为下一字节是新帧开头（taproot 同值） */
#define DR16_FRAME_GAP_US 6000u

typedef struct
{
    RcStateTopic *out;
    Watchdog wd; /* 只用于上线 / 离线日志和设备清单；丢失判定看话题新旧 */
    uint8_t frame[DR16_FRAME_LEN];
    uint8_t len;    /* frame 里已收到的字节数 */
    bool have_last; /* last_rx_us 是否有效 */
    uint64_t last_rx_us;
    uint32_t bad_frames; /* 校验不通过被丢弃的帧数（调试用） */
} Dr16;

/**
 * @brief   认领 RcState 话题、登记看门狗
 * @return  false：话题已被别的模块认领
 * @pre     初始化阶段调用
 */
RM_NODISCARD bool dr16_init(Dr16 *self, RcStateTopic *out);

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
