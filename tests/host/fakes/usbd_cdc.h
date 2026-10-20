/** STM32 CDC 状态替身；只覆盖平台驱动实际使用的字段。 */
#pragma once
#include <stdint.h>
typedef struct
{
    void *pClassData;
} USBD_HandleTypeDef;
typedef struct
{
    volatile uint32_t TxState;
} USBD_CDC_HandleTypeDef;
#define USBD_OK   0u
#define USBD_BUSY 1u
