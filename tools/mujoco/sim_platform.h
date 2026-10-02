/**
 * @file    sim_platform.h
 * @brief   单线程仿真的时间和虚拟 CAN 出口，不访问实际硬件
 */
#pragma once

#include <stdint.h>

/** 开始一个控制周期；now_us 为仿真时刻，同时清空本周期收到的电流命令。 */
void sim_platform_begin(uint64_t now_us);
/** @param id 虚拟 M3508 的 ID（1–8）。@return CAN 编码后还原的输出轴力矩，N·m。 */
float sim_platform_torque(uint8_t id);
