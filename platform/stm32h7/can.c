/**
 * @file    can.c
 * @brief   STM32H7 FDCAN 收发，见 platform/can.h
 * @note    - 每个订阅占一个“范围滤波器”（FDCAN_FILTER_RANGE，精确 ID 就是首尾相同的范围），其余帧全部拒收；
 *            可用的滤波器数量由 CubeMX 的 StdFiltersNbr 决定；
 *          - 接收用哪个 FIFO 按 CubeMX 配置自动选：FDCAN1/3 用 FIFO0，FDCAN2 用 FIFO1（COD-H7-Template 的配置）；
 *          - 做法参考 COD-H7-Template `bsp_can.c`（它用一个全放行的掩码滤波器，这里改为按订阅精确过滤）。
 */
#include "platform/can.h"

#include "platform/time.h"

#include "can_dlc.h"
#include "can_rx_ring.h"

#include "fdcan.h"

#include <stddef.h>

#define MAX_SUBSCRIPTIONS 16u /* 软件表的上限；实际还受 CubeMX 的 StdFiltersNbr 限制 */

typedef struct
{
    uint32_t first_id;
    uint32_t last_id;
    CanRxHandler handler;
    void *ctx;
} Subscription;

typedef struct
{
    Subscription subs[MAX_SUBSCRIPTIONS];
    uint32_t sub_count;
    CanRxRing ring; /* 中断写、comm_rx 任务读 */
    CanRxNotify notify;
    void *notify_ctx;
    bool started;
} CanBus;

static FDCAN_HandleTypeDef *const handles[CAN_BUS_COUNT] = {
    [CAN_BUS_1] = &hfdcan1,
    [CAN_BUS_2] = &hfdcan2,
    [CAN_BUS_3] = &hfdcan3,
};

static CanBus buses[CAN_BUS_COUNT];

/* CubeMX 给这路总线分配了 RX FIFO0 就用 FIFO0，否则用 FIFO1 */
static bool uses_fifo0(const FDCAN_HandleTypeDef *h)
{
    return h->Init.RxFifo0ElmtsNbr > 0u;
}

static bool ranges_overlap(uint32_t a_first, uint32_t a_last, uint32_t b_first, uint32_t b_last)
{
    return a_first <= b_last && b_first <= a_last;
}

bool can_subscribe_range(CanBusId bus, uint32_t first_id, uint32_t last_id, CanRxHandler handler,
                         void *ctx)
{
    CanBus *self = &buses[bus];
    const uint32_t hw_filters = handles[bus]->Init.StdFiltersNbr;
    if (self->started || handler == NULL || first_id > last_id || last_id > CAN_STD_ID_MAX
        || self->sub_count >= MAX_SUBSCRIPTIONS || self->sub_count >= hw_filters)
    {
        return false;
    }
    for (uint32_t i = 0u; i < self->sub_count; i++)
    {
        if (ranges_overlap(first_id, last_id, self->subs[i].first_id, self->subs[i].last_id))
        {
            return false;
        }
    }
    self->subs[self->sub_count] = (Subscription){ first_id, last_id, handler, ctx };
    self->sub_count++;
    return true;
}

bool can_subscribe(CanBusId bus, uint32_t id, CanRxHandler handler, void *ctx)
{
    return can_subscribe_range(bus, id, id, handler, ctx);
}

bool can_start(CanBusId bus, CanRxNotify notify, void *ctx)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    CanBus *self = &buses[bus];
    const bool fifo0 = uses_fifo0(h);

    for (uint32_t i = 0u; i < self->sub_count; i++)
    {
        FDCAN_FilterTypeDef filter = {
            .IdType = FDCAN_STANDARD_ID,
            .FilterIndex = i,
            .FilterType = FDCAN_FILTER_RANGE,
            .FilterConfig = fifo0 ? FDCAN_FILTER_TO_RXFIFO0 : FDCAN_FILTER_TO_RXFIFO1,
            .FilterID1 = self->subs[i].first_id,
            .FilterID2 = self->subs[i].last_id,
        };
        if (HAL_FDCAN_ConfigFilter(h, &filter) != HAL_OK)
        {
            return false;
        }
    }

    /* 没有匹配任何滤波器的帧（标准帧、扩展帧、远程帧）一律拒收 */
    if (HAL_FDCAN_ConfigGlobalFilter(h, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE)
        != HAL_OK)
    {
        return false;
    }

    self->notify = notify;
    self->notify_ctx = ctx;
    const uint32_t rx_it = fifo0 ? FDCAN_IT_RX_FIFO0_NEW_MESSAGE : FDCAN_IT_RX_FIFO1_NEW_MESSAGE;
    if (HAL_FDCAN_ActivateNotification(h, rx_it, 0u) != HAL_OK || HAL_FDCAN_Start(h) != HAL_OK)
    {
        return false;
    }
    self->started = true;
    return true;
}

uint32_t can_dispatch(CanBusId bus)
{
    CanBus *self = &buses[bus];
    CanFrame frame;
    uint32_t count = 0u;

    while (can_rx_ring_pop(&self->ring, &frame))
    {
        for (uint32_t i = 0u; i < self->sub_count; i++)
        {
            const Subscription *sub = &self->subs[i];
            if (frame.id >= sub->first_id && frame.id <= sub->last_id)
            {
                sub->handler(&frame, sub->ctx);
                break;
            }
        }
        count++;
    }
    return count;
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    uint8_t dlc = 0u;
    const bool bus_is_fd = h->Init.FrameFormat != FDCAN_FRAME_CLASSIC;

    if (frame->id > CAN_STD_ID_MAX || !can_len_to_dlc(frame->len, &dlc)
        || (!frame->is_fd && frame->len > 8u) || (frame->is_fd && !bus_is_fd))
    {
        return false;
    }

    FDCAN_TxHeaderTypeDef header = {
        .Identifier = frame->id,
        .IdType = FDCAN_STANDARD_ID,
        .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = dlc,
        .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch = (frame->is_fd && h->Init.FrameFormat == FDCAN_FRAME_FD_BRS)
                             ? FDCAN_BRS_ON
                             : FDCAN_BRS_OFF,
        .FDFormat = frame->is_fd ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN,
        .TxEventFifoControl = FDCAN_NO_TX_EVENTS,
        .MessageMarker = 0u,
    };

    /* 发送队列的写入不是可重入的：多个任务都可能发送，写入期间短暂关中断（几微秒） */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(h, &header, frame->data);
    __set_PRIMASK(primask);
    return status == HAL_OK;
}

uint32_t can_rx_dropped(CanBusId bus)
{
    return buses[bus].ring.dropped;
}

/* 中断上下文：把硬件 FIFO 里的帧全部搬进环形缓冲，然后通知任务 */
static void drain_rx_fifo(FDCAN_HandleTypeDef *h, uint32_t fifo)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (handles[bus] != h)
        {
            continue;
        }
        CanBus *self = &buses[bus];
        const uint64_t stamp_us = rm_time_now_us();
        FDCAN_RxHeaderTypeDef header;
        CanFrame frame = { 0 };

        while (HAL_FDCAN_GetRxFifoFillLevel(h, fifo) > 0u)
        {
            if (HAL_FDCAN_GetRxMessage(h, fifo, &header, frame.data) != HAL_OK)
            {
                break;
            }
            frame.id = header.Identifier;
            frame.len = can_dlc_to_len((uint8_t)header.DataLength);
            frame.is_fd = header.FDFormat == FDCAN_FD_CAN;
            frame.stamp_us = stamp_us;
            (void)can_rx_ring_push(&self->ring, &frame); /* 满了丢弃并计数，见 can_rx_dropped() */
        }
        if (self->notify != NULL)
        {
            self->notify(self->notify_ctx);
        }
        return;
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    (void)RxFifo0ITs;
    drain_rx_fifo(hfdcan, FDCAN_RX_FIFO0);
}

void HAL_FDCAN_RxFifo1Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo1ITs)
{
    (void)RxFifo1ITs;
    drain_rx_fifo(hfdcan, FDCAN_RX_FIFO1);
}
