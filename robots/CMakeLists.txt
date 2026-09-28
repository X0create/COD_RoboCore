# 当前构建的兵种。阶段 0 只有样板 _template；以后由预设用 RM_ROBOT 选择（如 infantry）
set(RM_ROBOT "_template" CACHE STRING "兵种目录名（robots/ 下）")

add_library(rm_robot STATIC
    common/app_main.c
    ${RM_ROBOT}/robot.c
)
target_include_directories(rm_robot PUBLIC common)
target_link_libraries(rm_robot PUBLIC rm_core)
rm_target_warnings(rm_robot)
