# 设备协议解析；只用 platform 接口、core、msgs，电脑测试和固件都链接它
add_library(rm_devices STATIC
    motor/dji_motor.c
    motor/motor.c
    motor/motor_group.c
    remote/dr16.c
)
# 以 "devices/remote/dr16.h" 的形式 include
target_include_directories(rm_devices PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_devices PUBLIC rm_msgs rm_core_common)
rm_target_warnings(rm_devices)
