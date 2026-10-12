# 一个外设一个目录：接口 <外设>.h、与芯片无关的辅助代码 <外设>.c、芯片实现 <外设>_<芯片>.c

# 与芯片无关的辅助代码（纯计算），只给本层的芯片实现用；电脑测试和固件都链接它
add_library(rm_platform_common STATIC
    can/can.c
    status_led/status_led.c
    time/time.c
    uart/uart.c
    usb_cdc/usb_cdc.c
)
# 以 "05_platform/can/can.h" 的形式 include
target_include_directories(rm_platform_common PUBLIC ${PROJECT_SOURCE_DIR})
rm_target_warnings(rm_platform_common)

# 当前主控的芯片实现：每个外设一份 <外设>_${RM_CHIP}.c，同名函数，CMake 只编译这一份（换芯片不改上层代码）
if(RM_CHIP)
    add_library(rm_platform STATIC
        adc/adc_${RM_CHIP}.c
        can/can_${RM_CHIP}.c
        gpio/gpio_${RM_CHIP}.c
        iwdg/iwdg_${RM_CHIP}.c
        pwm/pwm_${RM_CHIP}.c
        spi/spi_${RM_CHIP}.c
        status_led/status_led_${RM_CHIP}.c
        time/time_${RM_CHIP}.c
        uart/uart_${RM_CHIP}.c
        usb_cdc/usb_cdc_${RM_CHIP}.c
    )
    target_include_directories(rm_platform PUBLIC ${PROJECT_SOURCE_DIR})
    target_link_libraries(rm_platform
        PUBLIC rm_vendor
        PRIVATE rm_platform_common
    )
    rm_target_warnings(rm_platform)
endif()
