#include <math.h>
#include <stddef.h>
#include <stdint.h>

void jog_matmul(const float* a, const float* b, float* out);
void jog_weights(float* out);
void jog_int_add(const int64_t* a, const int64_t* b, int64_t* out);
void jog_int_matmul(const int64_t* a, const int64_t* b, int64_t* out);
float jog_affine(float x);
float jog_direct(float x);
float jog_relu(float x);

int main(void) {
  float weights[2] = {0.0f, 0.0f};
  jog_weights(weights);
  if (weights[0] != 1.0f || weights[1] != 2.0f)
    return 7;

  const int64_t integers_a[6] = {1, 2, 3, 4, 5, 6};
  const int64_t integers_b[6] = {7, 8, 9, 10, 11, 12};
  const int64_t integers_expected[6] = {8, 10, 12, 14, 16, 18};
  int64_t integers_out[6] = {0};
  jog_int_add(integers_a, integers_b, integers_out);
  for (size_t i = 0; i < 6; ++i)
    if (integers_out[i] != integers_expected[i])
      return 5;
  const int64_t product_expected[4] = {58, 64, 139, 154};
  int64_t product[4] = {0};
  jog_int_matmul(integers_a, integers_b, product);
  for (size_t i = 0; i < 4; ++i)
    if (product[i] != product_expected[i])
      return 6;

  const float a[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  const float b[6] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  const float expected[4] = {58.0f, 64.0f, 139.0f, 154.0f};
  float out[4] = {0.0f};
  jog_matmul(a, b, out);
  for (size_t i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  if (fabsf(jog_affine(3.0f) - 7.0f) > 1e-6f)
    return 2;
  if (fabsf(jog_direct(3.0f) - 6.0f) > 1e-6f)
    return 3;
  if (fabsf(jog_relu(-2.0f)) > 1e-6f ||
      fabsf(jog_relu(3.0f) - 3.0f) > 1e-6f)
    return 4;
  return 0;
}
