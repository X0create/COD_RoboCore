cmake_minimum_required(VERSION 3.25)
project(COD_RoboCore LANGUAGES C)

# C11 + GNU 扩展：CubeMX 生成的工程也用这个设置，避免两边不一致
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# 生成 compile_commands.json，供 clangd、clang-tidy 使用
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(warnings)