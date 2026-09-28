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
