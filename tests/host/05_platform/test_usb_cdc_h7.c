/** H723 实际平台驱动：异步忙重试、缓冲生命周期、发送完成只通知任务。 */
#include "05_platform/usb_cdc/usb_cdc_stm32h7.c"

#include "unity.h"

USBD_HandleTypeDef hUsbDeviceHS;
static USBD_CDC_HandleTypeDef cdc;
static uint8_t *in_flight;
static uint16_t in_flight_length;
static unsigned notify_count, send_count, init_count;

void MX_USB_DEVICE_Init(void)
{
    ++init_count;
}

uint8_t CDC_Transmit_HS(uint8_t *buf, uint16_t len)
{
    cdc.TxState = 1u;
    in_flight = buf;
    in_flight_length = len;
    ++send_count;
    return USBD_OK;
}

static void notify_task(void *ctx)
{
    TEST_ASSERT_EQUAL_PTR(&cdc, ctx);
    ++notify_count;
}

void setUp(void)
{
    cdc.TxState = 0u;
    hUsbDeviceHS.pClassData = &cdc;
    notify_count = send_count = init_count = 0u;
    TEST_ASSERT_TRUE(usb_cdc_start(notify_task, &cdc));
}

void tearDown(void)
{
}

static void test_completion_wakes_retry_without_sending_in_isr(void)
{
    const uint8_t first[] = { 1, 2, 3 }, next[] = { 4, 5 };
    TEST_ASSERT_EQUAL_UINT(1, init_count);
    TEST_ASSERT_TRUE(usb_cdc_write(first, sizeof(first)));
    TEST_ASSERT_FALSE(usb_cdc_write(next, sizeof(next)));
    TEST_ASSERT_EQUAL_UINT16(sizeof(first), in_flight_length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(first, in_flight, sizeof(first));
    cdc.TxState = 0u; /* CDC DataIn 在调用完成钩子前释放缓冲。 */
    usb_cdc_tx_complete_isr();
    TEST_ASSERT_EQUAL_UINT(1, notify_count);
    TEST_ASSERT_EQUAL_UINT(1, send_count);
    TEST_ASSERT_TRUE(usb_cdc_write(next, sizeof(next))); /* 被唤醒的任务重试 */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(next, in_flight, sizeof(next));
}

static void test_no_callback_or_disconnected_usb(void)
{
    TEST_ASSERT_TRUE(usb_cdc_start(NULL, NULL));
    usb_cdc_tx_complete_isr();
    TEST_ASSERT_EQUAL_UINT(0, notify_count);
    hUsbDeviceHS.pClassData = NULL;
    const uint8_t byte = 42u;
    TEST_ASSERT_FALSE(usb_cdc_write(&byte, 1u));
    TEST_ASSERT_EQUAL_UINT(0, send_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_completion_wakes_retry_without_sending_in_isr);
    RUN_TEST(test_no_callback_or_disconnected_usb);
    return UNITY_END();
}
