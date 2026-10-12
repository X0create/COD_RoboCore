# 与芯片无关的部分（安全门、各机构），电脑测试和固件都链接它
add_library(rm_app_logic STATIC
    system/safety_gate.c
    modules/chassis/chassis.c
    modules/ins/ins.c
)
# 以 "01_applic/modules/chassis/chassis.h" 的形式 include
target_include_directories(rm_app_logic PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_app_logic PUBLIC rm_devices rm_algorithm rm_core_common)
rm_target_warnings(rm_app_logic)

# 以下只用于固件（依赖 FreeRTOS）
if(NOT RM_BOARD)
    return()
endif()

add_library(rm_app STATIC
    # 上电顺序
    system/app_main.c
    # 全部任务（每个任务一个文件）
    tasks/ins_task.c
    tasks/control_task.c
    tasks/comm_rx_task.c
    tasks/detect_task.c
    tasks/indicator_task.c
    tasks/log_task.c
    # 这台车的对象、初始化和任务表
    config/robot.c
)
target_link_libraries(rm_app PUBLIC rm_app_logic rm_core rm_devices rm_algorithm)
# 所选主控板的目录：<board.h>（DBUS 串口、电池分压比这类板级接线）
target_include_directories(rm_app PRIVATE ${RM_BOARD_DIR})
rm_target_warnings(rm_app)
