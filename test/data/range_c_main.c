#include <stdint.h>
#include <stdbool.h>

void range_c_main(int32_t* result, int64_t* result_dim_0);
void range_c_fractional(float* result, int64_t* result_dim_0);
void range_c_bounded(bool choose, int32_t* result, int64_t* result_dim_0);
void range_c_capped(const int32_t* x, int32_t* result, int64_t* result_dim_0);
void range_c_descending(const int32_t* x, int32_t* result, int64_t* result_dim_0);

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
  const int32_t limits[] = {INT32_MIN, -101, -100, -1, 0, 1, 99, 100, 101, INT32_MAX};
  for (unsigned trial = 0; trial < sizeof(limits) / sizeof(limits[0]); ++trial) {
    for (int descending = 0; descending <= 1; ++descending) {
      int32_t values[102];
      for (int i = 0; i < 102; ++i) values[i] = -999;
      int64_t length = -1;
      const int32_t x = limits[trial];
      int64_t wanted;
      if (descending) {
        range_c_descending(&x, values + 1, &length);
        wanted = x >= 0 ? 0 : x <= -100 ? 100 : -(int64_t)x;
      } else {
        range_c_capped(&x, values + 1, &length);
        wanted = x <= 0 ? 0 : x >= 100 ? 100 : x;
      }
      if (length != wanted || values[0] != -999 || values[101] != -999) return 7;
      for (int64_t i = 0; i < wanted; ++i)
        if (values[i + 1] != (descending ? -i : i)) return 8;
      for (int64_t i = wanted; i < 100; ++i)
        if (values[i + 1] != -999) return 9;
    }
  }
  return 0;
}
