# 纯计算：不依赖芯片和 FreeRTOS，电脑测试和固件都链接它
add_library(rm_algorithm STATIC
    attitude/attitude.c
    control/control.c
    filter/filter.c
    kinematics/kinematics.c
    math/rm_math.c
    power/rls.c
)
# 以 "03_algorithm/control/control.h" 的形式 include
target_include_directories(rm_algorithm PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_algorithm PUBLIC m)
rm_target_warnings(rm_algorithm)
