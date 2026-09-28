# 纯计算的小工具：不依赖芯片和 FreeRTOS，电脑测试和固件都链接它
add_library(rm_core_util STATIC
    util/crc.c
)
# 以 "core/util/crc.h" 的形式 include
target_include_directories(rm_core_util PUBLIC ${PROJECT_SOURCE_DIR})
rm_target_warnings(rm_core_util)

# 以下只用于固件（依赖 FreeRTOS）；电脑侧的假实现以后再加
if(NOT RM_BOARD)
    return()
endif()

# SEGGER RTT（第三方代码）：不套用本仓库的严格警告
add_library(segger_rtt STATIC
    log/segger_rtt/SEGGER_RTT.c
    log/segger_rtt/SEGGER_RTT_printf.c
    log/segger_rtt/SEGGER_RTT_ASM_ARMv7M.S
)
# log/ 下放本项目的 SEGGER_RTT_Conf.h
target_include_directories(segger_rtt SYSTEM PUBLIC log/segger_rtt log)

add_library(rm_core STATIC
    log/log.c
    os/os.c
)
# 以 "core/log/log.h" 的形式 include
target_include_directories(rm_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_core PUBLIC rm_core_util rm_platform segger_rtt rm_vendor)
rm_target_warnings(rm_core)
