# 本仓库自己写的代码统一用这组警告，并把警告当错误。
# CMake 遇到警告仍会返回成功，所以只能靠 -Werror 让构建失败（UniC 的教训）。
# CubeMX 生成代码和第三方库不调用这个函数：它们的警告我们改不了。
function(rm_target_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wshadow               # 内层变量和外层同名
        -Wundef                # #if 里用了未定义的宏（会被当成 0）
        -Wdouble-promotion     # float 被悄悄提升成 double，M7/M4 上很慢
        -Wfloat-conversion     # double 结果悄悄截成 float
        -Wstrict-prototypes    # 无参数函数必须写成 f(void)
        -Wmissing-prototypes   # 非 static 函数必须有头文件声明，私有函数要加 static
        -Werror
    )
endfunction()