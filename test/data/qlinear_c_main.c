#include <stdint.h>

void qlinear_c_matmul(const uint8_t* a, const float* scale,
                     const uint8_t* zero, const int8_t* b,
                     const int8_t* b_zero,
                     uint8_t* result);
void qlinear_c_conv(const uint8_t* x, const float* input_scale,
                    const uint8_t* input_zero, const int8_t* weight,
                    const float* weight_scale, const int8_t* weight_zero,
                    const float* output_scale, const uint8_t* output_zero,
                    const int32_t* bias, uint8_t* result);
void qlinear_c_add(const uint8_t* a, const float* a_scale,
                   const uint8_t* a_zero, const uint8_t* b,
                   const float* b_scale, const uint8_t* b_zero,
                   const float* output_scale, const uint8_t* output_zero,
                   uint8_t* result);
void qlinear_c_avg_pool(const uint8_t* x, const float* input_scale,
                        const uint8_t* input_zero,
                        const float* output_scale,
                        const uint8_t* output_zero, uint8_t* result);

int main(void) {
  const uint8_t matmul_expected[4] = {1, 2, 3, 4};
  const uint8_t conv_expected[8] = {2, 3, 4, 5, 1, 3, 5, 7};
  const uint8_t add_expected[6] = {11, 22, 33, 14, 25, 36};
  const uint8_t pool_expected[4] = {1, 3, 5, 7};
  const uint8_t a[4] = {1, 2, 3, 4};
  const int8_t b[4] = {1, 0, 0, 1};
  const uint8_t x[4] = {1, 2, 3, 4};
  const uint8_t add_a[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t add_b[3] = {10, 20, 30};
  const uint8_t pool_x[16] = {
      1, 1, 3, 3, 1, 1, 3, 3,
      5, 5, 7, 7, 5, 5, 7, 7};
  const int8_t weight[2] = {1, 2};
  const int32_t bias[2] = {1, -1};
  const float scalar_scale[1] = {1.0f};
  const float weight_scale[2] = {1.0f, 1.0f};
  const uint8_t scalar_zero[1] = {0};
  const int8_t signed_zero[2] = {0, 0};
  uint8_t matmul[4] = {0};
  uint8_t conv[8] = {0};
  uint8_t add[6] = {0};
  uint8_t pool[4] = {0};
  qlinear_c_matmul(a, scalar_scale, scalar_zero, b, signed_zero, matmul);
  qlinear_c_conv(x, scalar_scale, scalar_zero, weight,
                 weight_scale, signed_zero, scalar_scale, scalar_zero,
                 bias, conv);
  qlinear_c_add(add_a, scalar_scale, scalar_zero, add_b,
                scalar_scale, scalar_zero, scalar_scale, scalar_zero, add);
  qlinear_c_avg_pool(pool_x, scalar_scale, scalar_zero,
                     scalar_scale, scalar_zero, pool);
  for (int i = 0; i < 4; ++i)
    if (matmul[i] != matmul_expected[i])
      return 1;
  for (int i = 0; i < 8; ++i)
    if (conv[i] != conv_expected[i])
      return 2;
  for (int i = 0; i < 6; ++i)
    if (add[i] != add_expected[i])
      return 3;
  for (int i = 0; i < 4; ++i)
    if (pool[i] != pool_expected[i])
      return 4;
  return 0;
}
