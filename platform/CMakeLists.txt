# 与芯片无关、各平台实现共用的纯计算；电脑测试和固件都链接它
add_library(rm_platform_common STATIC
    common/cycle_extend.c
)
target_include_directories(rm_platform_common PUBLIC common)
rm_target_warnings(rm_platform_common)
