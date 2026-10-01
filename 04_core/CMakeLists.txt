# 与芯片无关的部分：不直接依赖 FreeRTOS 和 HAL，电脑测试和固件都链接它。
# 看门狗要用 rm_critical_*()（04_core/os/critical.h）和 rm_time_now_us()（05_platform/time.h）：
# 固件里由 rm_core、rm_platform 提供，电脑测试里由 tests/host/fakes 提供
add_library(rm_core_common STATIC
    util/crc.c
    watchdog/watchdog.c
)
# 以 "04_core/util/crc.h"、"04_core/watchdog/watchdog.h" 的形式 include
target_include_directories(rm_core_common PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_core_common PUBLIC rm_platform_common)
rm_target_warnings(rm_core_common)

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
# 以 "04_core/log/log.h" 的形式 include
target_include_directories(rm_core PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_core PUBLIC rm_core_common rm_platform segger_rtt rm_vendor)
rm_target_warnings(rm_core)
