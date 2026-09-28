# 达妙 DM-MC02（STM32H723VGT6）：芯片编译选项和链接选项。
# 必须在创建任何目标之前 include，这些选项对本仓库代码和 CubeMX 生成代码都生效。
set(RM_BOARD_DIR ${CMAKE_SOURCE_DIR}/boards/dm_mc02_h723)

set(RM_CPU_FLAGS
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16         # H723 带双精度 FPU，我们只用 float
    -mfloat-abi=hard
)

add_compile_options(${RM_CPU_FLAGS} -ffunction-sections -fdata-sections)
add_link_options(${RM_CPU_FLAGS}
    -T${RM_BOARD_DIR}/STM32H723xG_flash.ld
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map
    -Wl,--print-memory-usage
)

# CubeMX 生成的 cmake/stm32cubemx/CMakeLists.txt 会链接这个变量里的库
set(TOOLCHAIN_LINK_LIBRARIES m)

# 平台实现目录：platform/stm32h7
set(RM_CHIP stm32h7)

# 本仓库代码使用 HAL、CMSIS、FreeRTOS 头文件的入口。include 目录和宏取自 CubeMX 生成的 stm32cubemx 目标，
# 但作为 SYSTEM 目录，第三方头文件里的警告不算在我们头上（-Werror）
add_library(rm_vendor INTERFACE)
target_include_directories(rm_vendor SYSTEM INTERFACE
    $<TARGET_PROPERTY:stm32cubemx,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(rm_vendor INTERFACE
    $<TARGET_PROPERTY:stm32cubemx,INTERFACE_COMPILE_DEFINITIONS>)
