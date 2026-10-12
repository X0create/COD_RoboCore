/**
 * @file    can_stm32f4.c
 * @brief   STM32F4 bxCAN 收发，见 05_platform/can/can.h
 * @note    - F407 只有 CAN1、CAN2 两路，都是经典 CAN（没有 CAN FD）；CAN_BUS_3 在这块芯片上不存在；
 *          - 接收全部标准数据帧：滤波器配成全部放行（掩码全 0），扩展帧和远程帧在中断里丢掉；
 *            两路共用 28 个滤波器组，CAN1 用第 0 组，CAN2 从第 14 组开始（SlaveStartFilterBank = 14）；
 *          - 两路都只用接收 FIFO0，中断把帧放进软件环形缓冲，comm_rx_task 用 can_read() 取出后按 ID 交给对应设备；
 *          - bus-off 由硬件自动恢复（CubeMX 里 ABOM = ENABLE）：控制器在总线安静一会儿后自己回来，
 *            所以 can_recover() 什么都不做，can_is_bus_off() 只用来报告。
 */
#include "05_platform/can/can.h"

#include "05_platform/time/time.h"

#include <can.h>

#include <stddef.h>

#define CAN2_FIRST_FILTER_BANK 14u /* 28 个滤波器组：0–13 给 CAN1，14–27 给 CAN2 */

typedef struct
{
    CanRxRing ring; /* 中断写、comm_rx_task 读 */
    CanRxNotify notify;
    void *notify_ctx;
    bool started;
} CanBus;

/* C 板上的两路；CAN_BUS_3 为 NULL（这块芯片没有） */
static CAN_HandleTypeDef *const handles[CAN_BUS_COUNT] = {
    [CAN_BUS_1] = &hcan1,
    [CAN_BUS_2] = &hcan2,
};

static CanBus buses[CAN_BUS_COUNT];

bool can_bus_exists(CanBusId bus)
{
    return handles[bus] != NULL;
}

/* 一个滤波器组配成“全部放行”：ID 和掩码都为 0，任何帧都匹配，收进 FIFO0 */
static bool accept_all(CAN_HandleTypeDef *h, uint32_t bank)
{
    const CAN_FilterTypeDef filter = {
        .FilterIdHigh = 0u,
        .FilterIdLow = 0u,
        .FilterMaskIdHigh = 0u,
        .FilterMaskIdLow = 0u,
        .FilterFIFOAssignment = CAN_FILTER_FIFO0,
        .FilterBank = bank,
        .FilterMode = CAN_FILTERMODE_IDMASK,
        .FilterScale = CAN_FILTERSCALE_32BIT,
        .FilterActivation = CAN_FILTER_ENABLE,
        .SlaveStartFilterBank = CAN2_FIRST_FILTER_BANK,
    };
    return HAL_CAN_ConfigFilter(h, &filter) == HAL_OK;
}

bool can_start(CanBusId bus, CanRxNotify notify, void *ctx)
{
    CAN_HandleTypeDef *h = handles[bus];
    if (h == NULL)
    {
        return false;
    }
    CanBus *self = &buses[bus];
    const uint32_t bank = (bus == CAN_BUS_1) ? 0u : CAN2_FIRST_FILTER_BANK;
    if (!accept_all(h, bank))
    {
        return false;
    }
    self->notify = notify;
    self->notify_ctx = ctx;
    if (HAL_CAN_ActivateNotification(h, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK
        || HAL_CAN_Start(h) != HAL_OK)
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
    (void)bus;
    return false; /* bxCAN 不支持 CAN FD */
}

bool can_send(CanBusId bus, const CanFrame *frame)
{
    CAN_HandleTypeDef *h = handles[bus];
    if (h == NULL || frame->id > CAN_STD_ID_MAX || frame->is_fd || frame->len > 8u)
    {
        return false;
    }
    CAN_TxHeaderTypeDef header = {
        .StdId = frame->id,
        .IDE = CAN_ID_STD,
        .RTR = CAN_RTR_DATA,
        .DLC = frame->len,
        .TransmitGlobalTime = DISABLE,
    };
    uint32_t mailbox;

    /* 发送邮箱的分配不是可重入的：多个任务都可能发送，写入期间短暂关中断（几微秒）。
     * 三个邮箱都满时 HAL 返回错误，这一帧丢弃（周期消息下个周期发新的） */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const HAL_StatusTypeDef status = HAL_CAN_AddTxMessage(h, &header, frame->data, &mailbox);
    __set_PRIMASK(primask);
    return status == HAL_OK;
}

bool can_is_bus_off(CanBusId bus)
{
    return buses[bus].started && (handles[bus]->Instance->ESR & CAN_ESR_BOFF) != 0u;
}

void can_recover(CanBusId bus)
{
    (void)bus; /* 硬件自动恢复（ABOM），不需要软件重启 */
}

uint32_t can_rx_dropped(CanBusId bus)
{
    return buses[bus].ring.dropped;
}

/* 中断上下文：把硬件 FIFO0 里的帧全部搬进环形缓冲，然后通知任务 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    for (int bus = 0; bus < (int)CAN_BUS_COUNT; bus++)
    {
        if (handles[bus] != hcan)
        {
            continue;
        }
        CanBus *self = &buses[bus];
        const uint64_t stamp_us = rm_time_now_us();
        CAN_RxHeaderTypeDef header;
        CanFrame frame = { 0 };

        while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0u)
        {
            if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &header, frame.data) != HAL_OK)
            {
                break;
            }
            if (header.IDE != CAN_ID_STD || header.RTR != CAN_RTR_DATA)
            {
                continue; /* 只要标准数据帧：滤波器全部放行，扩展帧、远程帧在这里丢掉 */
            }
            frame.id = header.StdId;
            frame.len =
                (uint8_t)(header.DLC > 8u ? 8u : header.DLC); /* 经典帧 DLC 9–15 也只有 8 字节 */
            frame.is_fd = false;
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
