/**
 * @file    compiler.h
 * @brief   编译器属性的统一写法；所有层都可以 include
 */
#pragma once

/** 返回值必须被使用：忽略时编译器报警（本仓库 -Werror 下即编译失败） */
#define RM_NODISCARD __attribute__((warn_unused_result))
