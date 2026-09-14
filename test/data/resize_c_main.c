#include <stdint.h>

void resize_c_nearest(const float* x, const int64_t* sizes, float* result);
void resize_c_linear(const float* x, const int64_t* sizes, float* result);

static int close(float left, float right) {
  float error = left - right;
  if (error < 0.0f)
    error = -error;
  return error <= 0.00001f;
}

int main(void) {
  const float input[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  const int64_t nearest_sizes[4] = {1, 1, 4, 4};
  const float nearest_expected[16] = {
      1.0f, 1.0f, 2.0f, 2.0f,
      1.0f, 1.0f, 2.0f, 2.0f,
      3.0f, 3.0f, 4.0f, 4.0f,
      3.0f, 3.0f, 4.0f, 4.0f};
  float nearest[16] = {0.0f};
  resize_c_nearest(input, nearest_sizes, nearest);
  for (int i = 0; i < 16; ++i)
    if (!close(nearest[i], nearest_expected[i]))
      return 1;

  const int64_t linear_sizes[4] = {1, 1, 3, 3};
  const float linear_expected[9] = {
      1.0f, 1.6666667f, 2.0f,
      2.3333333f, 3.0f, 3.3333333f,
      3.0f, 3.6666667f, 4.0f};
  float linear[9] = {0.0f};
  resize_c_linear(input, linear_sizes, linear);
  for (int i = 0; i < 9; ++i)
    if (!close(linear[i], linear_expected[i]))
      return 2;
  return 0;
}
