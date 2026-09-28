# 子系统：各机构的闭环；只用 devices、algorithm、core、msgs，电脑测试和固件都链接它
add_library(rm_subsystems STATIC
    ins/ins.c
)
# 以 "subsystems/ins/ins.h" 的形式 include
target_include_directories(rm_subsystems PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_subsystems PUBLIC rm_devices rm_algorithm rm_msgs)
rm_target_warnings(rm_subsystems)
