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
  const float values[] = {3.0f, -2.0f, 7.0f, 1.0f};
  float low = 0.0f;
  float high = 0.0f;
  model_extrema(values, &low, &high);
  if (low != -2.0f || high != 7.0f)
    return 2;
  return 0;
}
