/**
 * @file    test_matrix.c
 * @brief   matrix 的单元测试：加减、乘、转置、求逆（含需要换行的主元、奇异矩阵、6×6）
 */
#include "03_algorithm/math/matrix.h"

#include "unity.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static void test_add_sub_in_place(void)
{
    float a_data[4] = { 1, 2, 3, 4 };
    float b_data[4] = { 10, 20, 30, 40 };
    Matrix a, b;
    matrix_init(&a, 2, 2, a_data);
    matrix_init(&b, 2, 2, b_data);
    matrix_add(&a, &b, &a);
    const float sum[4] = { 11, 22, 33, 44 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(sum, a_data, 4);
    matrix_sub(&a, &b, &a);
    const float back[4] = { 1, 2, 3, 4 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(back, a_data, 4);
}

/* 2×3 乘 3×2，输出的行列数由函数设置 */
static void test_mul_sets_shape(void)
{
    float a_data[6] = { 1, 2, 3, 4, 5, 6 };
    float b_data[6] = { 7, 8, 9, 10, 11, 12 };
    float out_data[9] = { 0 };
    Matrix a, b, out;
    matrix_init(&a, 2, 3, a_data);
    matrix_init(&b, 3, 2, b_data);
    matrix_init(&out, 3, 3, out_data);
    matrix_mul(&a, &b, &out);
    TEST_ASSERT_EQUAL_UINT8(2, out.rows);
    TEST_ASSERT_EQUAL_UINT8(2, out.cols);
    const float expect[4] = { 58, 64, 139, 154 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expect, out_data, 4);
}

static void test_trans(void)
{
    float a_data[6] = { 1, 2, 3, 4, 5, 6 };
    float out_data[6];
    Matrix a, out;
    matrix_init(&a, 2, 3, a_data);
    matrix_init(&out, 1, 1, out_data);
    matrix_trans(&a, &out);
    TEST_ASSERT_EQUAL_UINT8(3, out.rows);
    TEST_ASSERT_EQUAL_UINT8(2, out.cols);
    const float expect[6] = { 1, 4, 2, 5, 3, 6 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expect, out_data, 6);
}

/* 行列式为 -1，逆矩阵是整数（伴随矩阵手算）：A * A⁻¹ 逐项验证过 */
static void test_inv_3x3_known(void)
{
    float a_data[9] = { 2, 1, 1, 1, 3, 2, 1, 0, 0 };
    float inv_data[9];
    Matrix a, inv;
    matrix_init(&a, 3, 3, a_data);
    matrix_init(&inv, 3, 3, inv_data);
    TEST_ASSERT_TRUE(matrix_inv(&a, &inv));
    const float expect[9] = { 0, 0, 1, -2, 1, 3, 3, -1, -5 };
    for (int i = 0; i < 9; i++)
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, expect[i], inv_data[i]);
    }
}

/* 第一列主元为 0，必须换行才能消元 */
static void test_inv_needs_row_swap(void)
{
    float a_data[4] = { 0, 1, 1, 0 };
    float inv_data[4];
    Matrix a, inv;
    matrix_init(&a, 2, 2, a_data);
    matrix_init(&inv, 2, 2, inv_data);
    TEST_ASSERT_TRUE(matrix_inv(&a, &inv));
    const float expect[4] = { 0, 1, 1, 0 };
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expect, inv_data, 4);
}

static void test_inv_singular_returns_false(void)
{
    float a_data[4] = { 1, 2, 2, 4 };
    float inv_data[4];
    Matrix a, inv;
    matrix_init(&a, 2, 2, a_data);
    matrix_init(&inv, 2, 2, inv_data);
    TEST_ASSERT_FALSE(matrix_inv(&a, &inv));
}

/* 6×6 对称正定（EKF 的规模）：A * A⁻¹ ≈ I */
static void test_inv_6x6_times_original_is_identity(void)
{
    float a_data[36], copy_data[36], inv_data[36], prod_data[36];
    for (int r = 0; r < 6; r++)
    {
        for (int c = 0; c < 6; c++)
        {
            a_data[r * 6 + c] = (r == c) ? 10.0f + (float)r : 1.0f / (float)(1 + r + c);
        }
    }
    for (int i = 0; i < 36; i++)
    {
        copy_data[i] = a_data[i];
    }
    Matrix a, copy, inv, prod;
    matrix_init(&a, 6, 6, a_data);
    matrix_init(&copy, 6, 6, copy_data);
    matrix_init(&inv, 6, 6, inv_data);
    matrix_init(&prod, 6, 6, prod_data);
    TEST_ASSERT_TRUE(matrix_inv(&copy, &inv)); /* copy 被破坏，a 保留 */
    matrix_mul(&a, &inv, &prod);
    for (int r = 0; r < 6; r++)
    {
        for (int c = 0; c < 6; c++)
        {
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, (r == c) ? 1.0f : 0.0f, prod_data[r * 6 + c]);
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_sub_in_place);
    RUN_TEST(test_mul_sets_shape);
    RUN_TEST(test_trans);
    RUN_TEST(test_inv_3x3_known);
    RUN_TEST(test_inv_needs_row_swap);
    RUN_TEST(test_inv_singular_returns_false);
    RUN_TEST(test_inv_6x6_times_original_is_identity);
    return UNITY_END();
}
