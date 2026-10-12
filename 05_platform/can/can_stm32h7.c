/**
 * @file    can_stm32h7.c
 * @brief   STM32H7 FDCAN 收发，见 05_platform/can/can.h
 * @note    - 接收全部标准数据帧，不用硬件滤波（滤波器配成全部放行）；
 *          - 接收用哪个 FIFO 按 CubeMX 配置自动选：FDCAN1/3 用 FIFO0，FDCAN2 用 FIFO1；
 *          - 总线分配：FDCAN1、FDCAN2 经典 CAN 1 Mbit/s 接电机（电机只认经典 CAN），
 *            FDCAN3 CAN FD + BRS（仲裁段 1 Mbit/s、数据段 5 Mbit/s）只给板间通信；
 *          - 中断把帧放进软件环形缓冲，comm_rx_task 用 can_read() 取出后按 ID 交给对应设备。
 */
#include "05_platform/can/can.h"

#include "05_platform/time/time.h"

#include <fdcan.h>

#include <stddef.h>

typedef struct
{
    CanRxRing ring; /* 中断写、comm_rx_task 读 */
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

/*
 * FD 总线的额外配置，在 HAL_FDCAN_Start 之前（初始化状态下）做：
 * 1. 消息 RAM 每格必须是 64 字节：CubeMX 默认 8 字节，FD 帧超过 8 字节的部分会被硬件悄悄截掉，所以在这里拒绝启动；
 * 2. 带速率切换（BRS）时打开发送延时补偿：数据段 5 Mbit/s 一位只有 200 ns，收发器的回环延迟占了一大截，
 *    不补偿时控制器在错误的时刻采样自己发出的位，报位错误。偏移取数据段采样点位置（预分频 × TSeg1，ST 例程的算法），
 *    按当前 1 / 14 / 5 配置为 14，即 (偏移 14, 滤波窗口 14)，这组值在 MC02 实车上用过。
 */
static bool configure_fd(FDCAN_HandleTypeDef *h, bool fifo0)
{
    const uint32_t rx_size = fifo0 ? h->Init.RxFifo0ElmtSize : h->Init.RxFifo1ElmtSize;
    if (rx_size != FDCAN_DATA_BYTES_64 || h->Init.TxElmtSize != FDCAN_DATA_BYTES_64)
    {
        return false;
    }
    if (h->Init.FrameFormat != FDCAN_FRAME_FD_BRS)
    {
        return true;
    }
    const uint32_t offset = h->Init.DataPrescaler * h->Init.DataTimeSeg1;
    return HAL_FDCAN_ConfigTxDelayCompensation(h, offset, offset) == HAL_OK
           && HAL_FDCAN_EnableTxDelayCompensation(h) == HAL_OK;
}

bool can_start(CanBusId bus, CanRxNotify notify, void *ctx)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    CanBus *self = &buses[bus];
    const bool fifo0 = uses_fifo0(h);

    /* 不配置滤波器（HAL_FDCAN_Init 已把滤波器区清零，即全部停用）：不匹配任何滤波器的标准数据帧收进接收 FIFO，
     * 扩展帧和远程帧拒收。帧交给谁由 comm_rx_task.c 按 ID 显式分派 */
    const uint32_t accept = fifo0 ? FDCAN_ACCEPT_IN_RX_FIFO0 : FDCAN_ACCEPT_IN_RX_FIFO1;
    if (HAL_FDCAN_ConfigGlobalFilter(h, accept, FDCAN_REJECT, FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE)
        != HAL_OK)
    {
        return false;
    }
    if (can_bus_is_fd(bus) && !configure_fd(h, fifo0))
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

/* 先 Stop 再 Start：直接清寄存器 CCCR.INIT 虽然也能让控制器回到总线，但会让 HAL 的
 * State 字段和硬件对不上，之后 HAL 拒绝发送。Stop / Start 不动消息 RAM 和中断使能，滤波器配置保留。 */
void can_recover(CanBusId bus)
{
    FDCAN_HandleTypeDef *h = handles[bus];
    if (HAL_FDCAN_Stop(h) == HAL_OK)
    {
        (void)HAL_FDCAN_Start(h); /* 失败时仍是 bus-off，comm_rx_task 100 ms 后再试 */
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
