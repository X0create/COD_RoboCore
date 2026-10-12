/**
 * @file    uart_stm32f4.c
 * @brief   STM32F4 串口接收：HAL 的 ReceiveToIdle + 循环 DMA，见 05_platform/uart/uart.h
 * @note    - 接收 DMA 必须在 CubeMX 里配置成循环模式（DMA_CIRCULAR），uart_rx_start() 会检查；
 *          - 中断里记下每次接收事件的累计字节数、位置和时刻（DmaRingWriter），任务关中断拷一份再读，
 *            所以能发现“绕了整圈”的覆盖，也能给出数据实际收到的时刻；
 *          - 串口号到 CubeMX 句柄的对应关系写在本文件（C 板：USART1、USART3 = DBUS、USART6）；
 *          - 出错时（包括 DBUS 的校验错误）F4 的 HAL 会停掉 DMA 接收，HAL_UART_ErrorCallback 里立刻重新开始；
 *          - 和 H7 版的差别只有两处：句柄表不同；F4 没有 Cache，DMA 缓冲区放普通内存即可，不用专用段。
 */
#include "05_platform/uart/uart.h"

#include "05_platform/time/time.h"

#include <usart.h>

#define RX_BUF_SIZE 256u

/* C 板的 CubeMX 工程里配置了的串口；没有列出的编号为 NULL */
static UART_HandleTypeDef *const handles[UART_COUNT] = {
    [UART_1] = &huart1,
    [UART_3] = &huart3,
    [UART_6] = &huart6,
};

/* 接收 DMA 的循环缓冲区：每个编号一块，没用到的编号也占 256 字节，换来不用按板子改数组大小 */
static uint8_t rx_buf[UART_COUNT][RX_BUF_SIZE];

typedef struct
{
    UartRxNotify notify;
    void *ctx;
    bool started;         /* uart_rx_start() 成功后置位 */
    DmaRingWriter writer; /* 只由中断改；任务关中断时整份拷贝 */
    DmaRingReader reader; /* 只由读取的任务改 */
} UartRx;

static UartRx rx[UART_COUNT];

/* 启动“DMA 循环接收 + 空闲中断”：DMA 不停地往 rx_buf 里循环写，空闲、半满、全满时进中断 */
static bool start_dma(UartPort port)
{
    return HAL_UARTEx_ReceiveToIdle_DMA(handles[port], rx_buf[port], RX_BUF_SIZE) == HAL_OK;
}

/* HAL 回调只给句柄：反查是哪个串口编号，不是本模块管理的串口返回 -1 */
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
    /* 板上没有这个串口、CubeMX 没给它配 RX DMA、或 DMA 不是循环模式：都打不开（循环模式是 REGEN_CHECKLIST 里的一项） */
    if (huart == NULL || huart->hdmarx == NULL || huart->hdmarx->Init.Mode != DMA_CIRCULAR)
    {
        return false;
    }
    rx[port].notify = notify;
    rx[port].ctx = ctx;
    rx[port].started = start_dma(port);
    return rx[port].started;
}

uint32_t uart_read(UartPort port, uint8_t *out, uint32_t max_len, uint64_t *rx_us)
{
    UartRx *self = &rx[port];
    if (!self->started)
    {
        return 0u; /* 没有打开（板上没有这个串口或 CubeMX 配置不对，打开时已报错）：句柄可能为空，不能读 */
    }

    /* 中断随时会更新记录：关中断拷一份，累计数、位置、时刻三者对得上 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const DmaRingWriter w = self->writer;
    __set_PRIMASK(primask);

    const uint32_t n = dma_ring_read(&self->reader, &w, rx_buf[port], RX_BUF_SIZE, out, max_len);
    if (n > 0u)
    {
        *rx_us = w.stamp_us;
    }
    return n;
}

/* HAL 回调（中断上下文）：空闲、DMA 半满、全满时调用，size 是 DMA 当前写到的位置。记账并通知任务 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    const int port = port_of(huart);
    if (port < 0)
    {
        return;
    }
    dma_ring_on_event(&rx[port].writer, RX_BUF_SIZE, size, rm_time_now_us());
    if (rx[port].notify != NULL)
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
        dma_ring_on_restart(&rx[port].writer);
    }
}
