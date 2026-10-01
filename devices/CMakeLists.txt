# 设备协议解析；只用 platform 接口、core、algorithm，电脑测试和固件都链接它
add_library(rm_devices STATIC
    battery/battery.c
    buzzer/buzzer.c
    imu/bmi088.c
    motor/dji_motor.c
    motor/dm_motor.c
    motor/motor.c
    motor/motor_group.c
    referee/ref_frame.c
    remote/dr16.c
    remote/vt_link.c
    vision/vision_frame.c
    vision/vision_link.c
)
# 以 "devices/remote/dr16.h" 的形式 include
target_include_directories(rm_devices PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(rm_devices PUBLIC rm_core_common rm_algorithm)
rm_target_warnings(rm_devices)
# 固件里接口的实现在 rm_platform（spi、pwm、can……）和 rm_core（临界区、延时）；声明依赖，
# 链接器才会把它们排在 rm_devices 之后。电脑测试里由 tests/host/fakes 提供
if(TARGET rm_core)
    target_link_libraries(rm_devices PUBLIC rm_core)
endif()
