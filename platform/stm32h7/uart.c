/**
 * @file    uart.c
 * @brief   STM32H7 串口接收：HAL 的 ReceiveToIdle + 循环 DMA，见 platform/uart.h
 * @note    - 接收 DMA 必须在 CubeMX 里配置成循环模式（DMA_CIRCULAR），uart_rx_start() 会检查；
 *          - 写位置直接读 DMA 剩余计数得到，中断里不保存位置，任务与中断之间没有需要加锁的共享数据；
 *          - 串口号到 CubeMX 句柄的对应关系目前写在本文件；以后有第二块 H7 板时移到 boards/<板子>/board.c。
 */
#include "platform/uart.h"

#include "dma_buf.h"
#include "dma_ring.h"

#include "usart.h"

#define RX_BUF_SIZE 256u

/* DM-MC02 的 CubeMX 工程里配置了的串口；没有列出的编号为 NULL */
static UART_HandleTypeDef *const handles[UART_COUNT] = {
    [UART_1] = &huart1, [UART_2] = &huart2, [UART_3] = &huart3,
    [UART_5] = &huart5, [UART_7] = &huart7, [UART_10] = &huart10,
};

/*
 * 接收 DMA 的循环缓冲区：H7 的 DMA1/DMA2 访问不到 DTCM，必须放在 AXI SRAM 的 .dma_buf 段
 * （该段已由 MPU 设为不可缓存，ADR 0021）。每个编号一块，没用到的编号也占 256 字节，
 * 共 2.5 KB，换来不用按板子改数组大小。
 */
RM_DMA_BUF static uint8_t rx_buf[UART_COUNT][RX_BUF_SIZE];

typedef struct
{
    UartRxNotify notify;
    void *ctx;
    bool started;                    /* uart_rx_start() 成功后置位 */
    uint32_t read_pos;               /* 只由读取的任务改 */
    uint32_t seen_restarts;          /* 只由读取的任务改 */
    volatile uint32_t restart_count; /* 只由中断改：出错后重新启动接收的次数 */
} UartRx;

static UartRx rx[UART_COUNT];

static bool start_dma(UartPort port)
{
    return HAL_UARTEx_ReceiveToIdle_DMA(handles[port], rx_buf[port], RX_BUF_SIZE) == HAL_OK;
}

static int port_of(const UART_HandleTypeDef *huart)
{
    for (int i = 0; i < (int)UART_COUNT; i++)
    {
        if (handles[i] == huart)
        {
            return i;
        }
    }
    return -1;
}

bool uart_rx_start(UartPort port, UartRxNotify notify, void *ctx)
{
    UART_HandleTypeDef *huart = handles[port];
    if (huart == NULL || huart->hdmarx == NULL || huart->hdmarx->Init.Mode != DMA_CIRCULAR)
    {
        return false;
    }
    rx[port].notify = notify;
    rx[port].ctx = ctx;
    rx[port].started = start_dma(port);
    return rx[port].started;
}

uint32_t uart_read(UartPort port, uint8_t *out, uint32_t max_len)
{
    UartRx *self = &rx[port];

    /* 中断里重新启动过接收：DMA 从缓冲区开头重新写，读位置跟着归零 */
    const uint32_t restarts = self->restart_count;
    if (restarts != self->seen_restarts)
    {
        self->seen_restarts = restarts;
        self->read_pos = 0u;
    }

    /* DMA 剩余计数从 RX_BUF_SIZE 递减；循环模式下减到 0 的瞬间会被重装，取模把这一瞬间算作 0 */
    const uint32_t remaining = __HAL_DMA_GET_COUNTER(handles[port]->hdmarx);
    const uint32_t write_pos = (RX_BUF_SIZE - remaining) % RX_BUF_SIZE;

    return dma_ring_take(rx_buf[port], RX_BUF_SIZE, &self->read_pos, write_pos, out, max_len);
}

/* HAL 回调（中断上下文）：空闲、DMA 半满、全满时调用。只通知任务 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    (void)size;
    const int port = port_of(huart);
    if (port >= 0 && rx[port].notify != NULL)
    {
        rx[port].notify(rx[port].ctx);
    }
}

/*
 * HAL 回调（中断上下文）：串口出错。溢出（ORE）等错误会让 HAL 停掉 DMA 接收，
 * 不重新启动就再也收不到数据（遥控器断开重连时常见）。帧错误、噪声这类不停止接收的错误，
 * 此时接收仍在进行，重新启动会返回 BUSY，不影响。
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    const int port = port_of(huart);
    if (port >= 0 && rx[port].started && start_dma((UartPort)port))
    {
        rx[port].restart_count++;
    }
}
