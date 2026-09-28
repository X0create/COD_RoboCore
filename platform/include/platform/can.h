/**
 * @file    can.h
 * @brief   CAN / CAN FD 收发（《架构设计》核心机制第 1 节、运行时契约第 6 节）
 * @note    - 接收按“精确 ID 或精确范围”订阅，不用会多收的掩码；只支持 11 位标准 ID（DJI、达妙都用标准 ID）；
 *          - 中断里只把帧放进环形缓冲并调用通知回调，订阅者的回调在任务里由 can_dispatch() 调用；
 *          - 使用顺序：初始化阶段 can_subscribe*() → 调度器启动后 can_start() → 任务里 can_dispatch()。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/compiler.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
    CAN_BUS_1,
    CAN_BUS_2,
    CAN_BUS_3,
    CAN_BUS_COUNT,
} CanBusId;

#define CAN_STD_ID_MAX   0x7FFu
#define CAN_MAX_DATA_LEN 64u

typedef struct
{
    uint32_t id; /* 11 位标准 ID */
    uint8_t len; /* 数据字节数：经典帧 0–8，FD 帧 0–8、12、16、20、24、32、48、64 */
    bool is_fd; /* true：CAN FD 帧（只能发往全 FD 的总线，ADR 0023） */
    uint8_t data[CAN_MAX_DATA_LEN];
    uint64_t stamp_us; /* 接收时刻（rm_time_now_us），发送时忽略 */
} CanFrame;

/** 订阅回调，在调用 can_dispatch() 的任务里执行 */
typedef void (*CanRxHandler)(const CanFrame *frame, void *ctx);

/** 收到新帧时的通知，在**中断**里调用；只能做“通知任务”这类事 */
typedef void (*CanRxNotify)(void *ctx);

/**
 * @brief   订阅一段连续 ID [first_id, last_id]，占用一个硬件滤波器
 * @return  false：ID 不合法、与已有订阅重叠，或这路总线的硬件滤波器已用完（CubeMX 的 StdFiltersNbr）
 * @pre     在 can_start() 之前调用（初始化阶段）
 */
RM_NODISCARD bool can_subscribe_range(CanBusId bus, uint32_t first_id, uint32_t last_id,
                                      CanRxHandler handler, void *ctx);

/** 订阅一个 ID，等同于 can_subscribe_range(bus, id, id, …) */
RM_NODISCARD bool can_subscribe(CanBusId bus, uint32_t id, CanRxHandler handler, void *ctx);

/**
 * @brief   按已登记的订阅配置硬件滤波器（其余帧全部拒收），打开接收中断并启动总线
 * @param   notify  收到新帧时在中断里调用，可以为 NULL（只靠任务轮询 can_dispatch()）
 * @return  false：这块板没有这路总线，或 HAL 配置失败
 * @pre     调度器已经启动；每路总线只调用一次
 */
RM_NODISCARD bool can_start(CanBusId bus, CanRxNotify notify, void *ctx);

/**
 * @brief   把中断收下的帧逐个交给订阅者的回调
 * @return  本次分发的帧数
 * @pre     只由一个任务调用（comm_rx）
 */
uint32_t can_dispatch(CanBusId bus);

/**
 * @brief   放进硬件发送队列，不等待发送完成
 * @return  false：发送队列满、帧不合法（如 FD 帧发往经典总线），这一帧被丢弃
 * @note    可以在多个任务里调用（内部短暂关中断）；不能在中断里调用
 */
RM_NODISCARD bool can_send(CanBusId bus, const CanFrame *frame);

/** 接收环形缓冲满、被丢弃的帧数（调试用） */
uint32_t can_rx_dropped(CanBusId bus);

#ifdef __cplusplus
}
#endif
