/**
 * @file    board.h
 * @brief   达妙 DM-MC02：上层代码要知道的板级接线（哪个串口接遥控、电池分压比）
 * @note    每块主控板的目录里各有一份同名的 board.h，构建时只把所选板子的目录加进头文件搜索路径，
 *          所以用 `#include <board.h>`（和 CubeMX 生成的头文件一样用尖括号，路径随板子变）。
 *          只放“换板子就变、和车无关”的东西；和车有关的放 01_applic/config/robot.h。
 */
#pragma once

#include "05_platform/uart/uart.h"

/** DR16 遥控接收机：接 UART5 */
#define BOARD_DBUS_UART UART_5

/** 电池电压分压比（电池电压 / ADC 引脚电压）：MC02 板上按 1/11 分压 */
#define BOARD_BATTERY_DIVIDER 11.0f
