# 大疆 RoboMaster 开发板 C 型（STM32F407IGH6）：芯片编译选项和链接选项。
# 必须在创建任何目标之前 include，这些选项对本仓库代码和 CubeMX 生成代码都生效。
set(RM_BOARD_DIR ${CMAKE_SOURCE_DIR}/06_boards/dji_c_f407)

set(RM_CPU_FLAGS
    -mcpu=cortex-m4
    -mthumb
    -mfpu=fpv4-sp-d16      # F407 只有单精度 FPU：代码里的浮点一律用 float
    -mfloat-abi=hard
)

# -gdwarf-4：调试信息用 DWARF 4（GCC 15 默认 5），Keil 和 Ozone 都能读；只影响 ELF 里的调试段，不进 Flash
add_compile_options(${RM_CPU_FLAGS} -ffunction-sections -fdata-sections -gdwarf-4)
add_link_options(${RM_CPU_FLAGS}
    # 直接用 CubeMX 生成的链接脚本：F407 不需要 DMA 专用段（没有 Cache，DMA 能访问普通 SRAM），没有要加的东西
    -T${RM_BOARD_DIR}/STM32F407xx_FLASH.ld
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map
    -Wl,--print-memory-usage
)

# CubeMX 生成的 cmake/stm32cubemx/CMakeLists.txt 会链接这个变量里的库
set(TOOLCHAIN_LINK_LIBRARIES m)

# 芯片名：05_platform/<外设>/<外设>_stm32f4.c
set(RM_CHIP stm32f4)

# 本仓库代码使用 HAL、CMSIS、FreeRTOS 头文件的入口。include 目录和宏取自 CubeMX 生成的 stm32cubemx 目标，
# 但作为 SYSTEM 目录，第三方头文件里的警告不算在我们头上（-Werror）
add_library(rm_vendor INTERFACE)
target_include_directories(rm_vendor SYSTEM INTERFACE
    $<TARGET_PROPERTY:stm32cubemx,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(rm_vendor INTERFACE
    $<TARGET_PROPERTY:stm32cubemx,INTERFACE_COMPILE_DEFINITIONS>)
