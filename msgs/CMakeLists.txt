# 消息类型与话题的认领 / 发布 / 读取函数；与芯片无关，电脑测试和固件都链接它
add_library(rm_msgs STATIC
    imu_state.c
    rc_state.c
)
# 以 "msgs/rc_state.h" 的形式 include
target_include_directories(rm_msgs PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_msgs PUBLIC rm_core_common)
rm_target_warnings(rm_msgs)
