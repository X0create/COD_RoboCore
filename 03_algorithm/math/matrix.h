/**
 * @file    matrix.h
 * @brief   小矩阵运算：加、减、乘、转置、求逆（纯计算，电脑上可测）
 * @note    代替旧工程用的 CMSIS-DSP `arm_mat_*`（ADR 0029）。面向卡尔曼滤波这类不超过十几维的矩阵，
 *          按行存储，不做分块或 SIMD 优化。
 *
 *          输出矩阵的行列数由函数设置，调用者只需保证输出的 data 足够大；
 *          同一块临时存储可以先后当作不同形状的矩阵使用。维数是否匹配属于调用约定，不在运行时检查。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** 按行存储：第 r 行第 c 列是 data[r * cols + c] */
typedef struct
{
    uint8_t rows;
    uint8_t cols;
    float *data;
} Matrix;

/** 把调用方提供的 rows × cols 个 float 包装成矩阵（不分配内存、不清零） */
void matrix_init(Matrix *m, uint8_t rows, uint8_t cols, float *data);

/** 所有元素置 0 */
void matrix_zero(Matrix *m);

/** out = a + b；out 可以就是 a 或 b */
void matrix_add(const Matrix *a, const Matrix *b, Matrix *out);

/** out = a - b；out 可以就是 a 或 b */
void matrix_sub(const Matrix *a, const Matrix *b, Matrix *out);

/** out = a * b；@pre a->cols == b->rows，out 与 a、b 不是同一块存储 */
void matrix_mul(const Matrix *a, const Matrix *b, Matrix *out);

/** out = aᵀ；@pre out 与 a 不是同一块存储 */
void matrix_trans(const Matrix *a, Matrix *out);

/**
 * @brief   out = a⁻¹（带列主元的高斯-约当消元）
 * @note    **a 的内容会被破坏**（与 arm_mat_inverse_f32 相同），需要保留时先复制。
 * @pre     a 是方阵，out 与 a 不是同一块存储
 * @return  false：a 奇异（某一列找不到非零主元），out 内容无意义
 */
bool matrix_inv(Matrix *a, Matrix *out);

#ifdef __cplusplus
}
#endif
