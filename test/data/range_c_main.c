#include <stdint.h>
#include <stdbool.h>

void range_c_main(int32_t* result, int64_t* result_dim_0);
void range_c_fractional(float* result, int64_t* result_dim_0);
void range_c_bounded(bool choose, int32_t* result, int64_t* result_dim_0);

int main(void) {
  const int32_t expected[4] = {9, 6, 3, 0};
  int32_t result[4] = {0};
  int64_t count = -1;
  range_c_main(result, &count);
  if (count != 4)
    return 1;
  for (int64_t i = 0; i < count; ++i)
    if (result[i] != expected[i])
      return 2;
  const float fractional_expected[3] = {0.5f, 1.1f, 1.7f};
  float fractional[3] = {0.0f};
  int64_t fractional_count = -1;
  range_c_fractional(fractional, &fractional_count);
  if (fractional_count != 3)
    return 3;
  for (int64_t i = 0; i < fractional_count; ++i) {
    float error = fractional[i] - fractional_expected[i];
    if (error < 0.0f)
      error = -error;
    if (error > 0.00001f)
      return 4;
  }
  for (int trial = 0; trial < 2; ++trial) {
    int32_t bounded[4] = {-1, -1, -1, -1};
    int64_t length = -1;
    range_c_bounded(trial != 0, bounded, &length);
    if (length != (trial ? 4 : 1)) return 5;
    for (int64_t i = 0; i < length; ++i)
      if (bounded[i] != 2 * i) return 6;
  }
  return 0;
}
