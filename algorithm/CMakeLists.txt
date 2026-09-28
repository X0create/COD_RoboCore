# 纯计算：不依赖芯片和 FreeRTOS，电脑测试和固件都链接它
add_library(rm_algorithm STATIC
    control/pid.c
    control/ramp.c
    filter/lpf.c
)
# 以 "algorithm/control/pid.h" 的形式 include
target_include_directories(rm_algorithm PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_algorithm PUBLIC m)
rm_target_warnings(rm_algorithm)
