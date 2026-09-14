#include <stdint.h>

void dynamic_slice_c_main(
    const int32_t* x, const int64_t* starts, const int64_t* ends,
    const int64_t* axes, const int64_t* steps, int32_t* result,
    int64_t* result_dim_0, int64_t* result_dim_1);

static int check(const int32_t* actual, const int32_t* expected,
                 int64_t count) {
  for (int64_t i = 0; i < count; ++i)
    if (actual[i] != expected[i])
      return 0;
  return 1;
}

int main(void) {
  const int32_t input[20] = {
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
      10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  const int64_t axes[2] = {0, 1};
  int32_t result[20] = {0};
  int64_t rows = -1;
  int64_t columns = -1;

  const int64_t forward_starts[2] = {1, 0};
  const int64_t forward_ends[2] = {4, 5};
  const int64_t forward_steps[2] = {1, 2};
  const int32_t forward_expected[9] = {
      5, 7, 9, 10, 12, 14, 15, 17, 19};
  dynamic_slice_c_main(input, forward_starts, forward_ends, axes,
                       forward_steps, result, &rows, &columns);
  if (rows != 3 || columns != 3 ||
      !check(result, forward_expected, 9))
    return 1;

  const int64_t reverse_starts[2] = {3, 4};
  const int64_t reverse_ends[2] = {-5, -6};
  const int64_t reverse_steps[2] = {-1, -2};
  const int32_t reverse_expected[12] = {
      19, 17, 15, 14, 12, 10, 9, 7, 5, 4, 2, 0};
  dynamic_slice_c_main(input, reverse_starts, reverse_ends, axes,
                       reverse_steps, result, &rows, &columns);
  if (rows != 4 || columns != 3 ||
      !check(result, reverse_expected, 12))
    return 2;
  return 0;
}
