/**
 * @file    can.c
 * @brief   STM32H7 FDCAN 收发，见 05_platform/can/can.h
 * @note    - 接收全部标准数据帧，不用硬件滤波（同 COD-H7-Template `bsp_can.c` 的全放行，ADR 0049）；
 *          - 接收用哪个 FIFO 按 CubeMX 配置自动选：FDCAN1/3 用 FIFO0，FDCAN2 用 FIFO1（COD-H7-Template 的配置）；
 *          - 中断把帧放进软件环形缓冲，comm_rx 任务用 can_read() 取出后按 ID 交给对应设备。
 */
#include "05_platform/can/can.h"

#include "05_platform/time/time.h"

#include "can_dlc.h"
#include "can_rx_ring.h"

#include <fdcan.h>

#include <stddef.h>

typedef struct
{
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

bool can_start(CanBusId bus, CanRxNotify notify, void *ctx)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    CanBus *self = &buses[bus];
    const bool fifo0 = uses_fifo0(h);

    /* 不配置滤波器（HAL_FDCAN_Init 已把滤波器区清零，即全部停用）：不匹配任何滤波器的标准数据帧收进接收 FIFO，
     * 扩展帧和远程帧拒收。帧交给谁由兵种的 comm_rx_task.c 按 ID 显式分派 */
    const uint32_t accept = fifo0 ? FDCAN_ACCEPT_IN_RX_FIFO0 : FDCAN_ACCEPT_IN_RX_FIFO1;
    if (HAL_FDCAN_ConfigGlobalFilter(h, accept, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
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

bool can_read(CanBusId bus, CanFrame *out)
{
    return can_rx_ring_pop(&buses[bus].ring, out);
}

bool can_bus_is_fd(CanBusId bus)
{
    return handles[bus]->Init.FrameFormat != FDCAN_FRAME_CLASSIC;
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    uint8_t dlc = 0u;
    const bool bus_is_fd = can_bus_is_fd(bus);

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

bool can_is_bus_off(CanBusId bus)
{
    return buses[bus].started && (handles[bus]->Instance->PSR & FDCAN_PSR_BO) != 0u;
}

/* 先 Stop 再 Start（做法同 COD_UniCFramework `can_bus_off_recover`）：直接清 CCCR.INIT 会让 HAL 的
 * State 字段和硬件对不上，之后 HAL 拒绝发送。Stop / Start 不动消息 RAM 和中断使能，滤波器配置保留。 */
void can_recover(CanBusId bus)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    if (HAL_FDCAN_Stop(h) == HAL_OK)
    {
        (void)HAL_FDCAN_Start(h); /* 失败时仍是 bus-off，comm_rx 任务 100 ms 后再试 */
    }
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
