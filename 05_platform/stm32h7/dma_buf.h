/**
 * @file    dma_buf.h
 * @brief   把变量放进 DMA 专用段（仅 05_platform/stm32h7 内部使用，ADR 0021）
 * @note    H723 的 .bss 默认在 DTCM，DMA1/DMA2 访问不到；放进链接脚本 dm_mc02.ld 的 .dma_buf 段
 *          （AXI SRAM，MPU 设为不可缓存）后，CPU 和 DMA 看到的数据一致，驱动里不用写 Cache 维护代码。
 *          每个使用处都要写一行注释说明是哪个外设的 DMA 缓冲区，否则以后有人删掉这个宏，
 *          编译照样通过，运行时却什么都收不到（CODING_STANDARD 示例）。
 */
#pragma once

/* 32 字节对齐 = Cache 行大小；不在段里初始化（NOLOAD），使用前由驱动自己写 */
#define RM_DMA_BUF __attribute__((section(".dma_buf"), aligned(32)))
