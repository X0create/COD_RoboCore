/**
 * @file    board.h
 * @brief   大疆 C 板：上层代码要知道的板级接线（哪个串口接遥控、电池分压比）
 * @note    每块主控板的目录里各有一份同名的 board.h，构建时只把所选板子的目录加进头文件搜索路径，
 *          所以用 `#include <board.h>`（和 CubeMX 生成的头文件一样用尖括号，路径随板子变）。
 *          只放“换板子就变、和车无关”的东西；和车有关的放 01_applic/config/robot.h。
 */
#pragma once

#include "05_platform/uart/uart.h"

/** DR16 遥控接收机：板上 DBUS 口接 USART3（PC11，板上已有反相电路） */
#define BOARD_DBUS_UART UART_3

/**
 * 电池电压分压比（电池电压 / ADC 引脚电压）：C 板 PF10 前是 200 kΩ + 22 kΩ 分压，(200 + 22) / 22 ≈ 10.09。
 * 按 C 板常见资料推断，未用万用表核对；读数不准时量一下电池电压，对照着改
 */
#define BOARD_BATTERY_DIVIDER 10.09f
