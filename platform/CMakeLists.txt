# 与芯片无关、各平台实现共用的纯计算；电脑测试和固件都链接它
add_library(rm_platform_common STATIC
    common/can_dlc.c
    common/can_rx_ring.c
    common/cycle_extend.c
    common/dma_ring.c
    common/ws2812.c
)
# common/ 里的模块会用到接口头文件里的类型（如 CanFrame）
target_include_directories(rm_platform_common PUBLIC common include)
rm_target_warnings(rm_platform_common)

# 当前主控的平台实现：同名函数，CMake 只编译这一份（ADR 0017）
if(RM_CHIP)
    add_library(rm_platform STATIC
        ${RM_CHIP}/can.c
        ${RM_CHIP}/pwm.c
        ${RM_CHIP}/spi.c
        ${RM_CHIP}/status_led.c
        ${RM_CHIP}/time.c
        ${RM_CHIP}/uart.c
    )
    target_include_directories(rm_platform PUBLIC include)
    target_link_libraries(rm_platform
        PUBLIC rm_vendor
        PRIVATE rm_platform_common
    )
    rm_target_warnings(rm_platform)
endif()
