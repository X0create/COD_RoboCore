# 单电机时序诊断

`trace.h/.c`：MBT1 格式、2048 项环形缓冲及 arm / 触发 / 尾部 / 冻结状态机，可主机测试。
`trace_stm32h7.c`：DWT 时钟、保存 / 恢复 PRIMASK、内存发布屏障与 NVIC 覆盖快照。
仅 `h723-m3508-timing` 编译实际记录器；普通固件宏不求值，无记录缓冲。
调用与读取见 `docs/M3508_TIMING_CAPTURE.md`；未上板，不把插桩分布当作原固件时序。
