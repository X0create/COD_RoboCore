/**
 * @file    usb_cdc.c
 * @brief   USB 虚拟串口的 STM32H7 实现，见 05_platform/usb_cdc/usb_cdc.h
 * @note    - USB OTG HS 用内置全速 PHY、不开 DMA（CubeMX 配置），缓冲区放在哪块 RAM 都可以；
 *          - OTG_HS 中断优先级为 5，属于“RTOS 管理的中断”，可以在里面通知任务；
 *          - 接收钩子 usb_cdc_rx_isr() 由 CubeMX 生成的 usbd_cdc_if.c 在 USER CODE 区里调用
 *            （REGEN_CHECKLIST 第 11 条）；
 *          - MX_USB_DEVICE_Init() 原本只在 CubeMX 生成的弱定义 startup_task 里调用，框架覆盖了它，
 *            所以改在 usb_cdc_start() 里调用。
 */
#include "05_platform/usb_cdc/usb_cdc.h"

#include <string.h>

#include "byte_ring.h"
#include <usb_device.h>
#include <usbd_cdc.h>
#include <usbd_cdc_if.h>

extern USBD_HandleTypeDef hUsbDeviceHS;

/* 在 usbd_cdc_if.c 的 CDC_Receive_HS 里调用（USB 中断） */
void usb_cdc_rx_isr(const uint8_t *data, uint32_t len);

static ByteRing rx_ring;
static uint8_t tx_buf[USB_CDC_TX_MAX];
static UsbCdcRxNotify rx_notify;
static void *rx_ctx;

bool usb_cdc_start(UsbCdcRxNotify notify, void *ctx)
{
    rx_notify = notify;
    rx_ctx = ctx;
    MX_USB_DEVICE_Init(); /* 失败时 CubeMX 生成的代码会进 Error_Handler */
    return true;
}

void usb_cdc_rx_isr(const uint8_t *data, uint32_t len)
{
    (void)byte_ring_push(&rx_ring, data, len);
    if (rx_notify != NULL)
    {
        rx_notify(rx_ctx);
    }
}

uint32_t usb_cdc_read(uint8_t *out, uint32_t max_len)
{
    return byte_ring_pop(&rx_ring, out, max_len);
}

bool usb_cdc_write(const uint8_t *data, uint32_t len)
{
    const USBD_CDC_HandleTypeDef *cdc = (const USBD_CDC_HandleTypeDef *)hUsbDeviceHS.pClassData;
    /* 没枚举（没插线）时 pClassData 为空；TxState 非 0 表示上一包还在发，发送缓冲不能改 */
    if (len > USB_CDC_TX_MAX || cdc == NULL || cdc->TxState != 0u)
    {
        return false;
    }
    memcpy(tx_buf, data, len);
    return CDC_Transmit_HS(tx_buf, (uint16_t)len) == USBD_OK;
}

uint32_t usb_cdc_rx_dropped(void)
{
    return rx_ring.dropped;
}
