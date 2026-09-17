#include <stdint.h>

int main(void) {
  const float a[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  const float b[] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  const float expected[] = {58.0f, 64.0f, 139.0f, 154.0f};
  float out[4] = {0.0f};
  model_main(a, b, out);
  for (int64_t i = 0; i != 4; ++i) {
    float error = out[i] - expected[i];
    if (error < 0.0f)
      error = -error;
    if (error > 1.0e-5f)
      return 1;
  }
  const float wide_a[] = {1.0f, 2.0f, 3.0f};
  const float wide_b[] = {1.0f,  2.0f,  3.0f,  4.0f,
                          5.0f,  6.0f,  7.0f,  8.0f,
                          9.0f, 10.0f, 11.0f, 12.0f};
  const float wide_expected[] = {38.0f, 44.0f, 50.0f, 56.0f};
  float wide[4] = {0.0f};
  model_wide(wide_a, wide_b, wide);
  for (int64_t i = 0; i != 4; ++i)
    if (wide[i] != wide_expected[i])
      return 3;
  const float image[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
                         6.0f, 7.0f, 8.0f, 9.0f};
  const float filter[] = {1.0f, 0.0f, 0.0f, -1.0f};
  float convolved[4] = {0.0f};
  model_conv(image, filter, convolved);
  for (int64_t i = 0; i != 4; ++i)
    if (convolved[i] != -4.0f)
      return 4;
  float fixed[4] = {0.0f};
  model_fixed_conv(image, fixed);
  for (int64_t i = 0; i != 4; ++i)
    if (fixed[i] != -4.0f)
      return 5;
  const float bias[] = {5.0f};
  float biased[4] = {0.0f};
  model_biased_conv(image, filter, bias, biased);
  for (int64_t i = 0; i != 4; ++i)
    if (biased[i] != 1.0f)
      return 6;
  const float values[] = {3.0f, -2.0f, 7.0f, 1.0f};
  float low = 0.0f;
  float high = 0.0f;
  model_extrema(values, &low, &high);
  if (low != -2.0f || high != 7.0f)
    return 2;
  return 0;
}
