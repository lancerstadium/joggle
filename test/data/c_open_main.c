#include <math.h>

int main(void) {
  const float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  const float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  const float expected[4] = {6.0f, 8.0f, 10.0f, 12.0f};
  float out[4] = {0.0f};
  jog_add(a, b, out);
  for (int i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  if (jog_carry(3) != 10)
    return 2;
  const float sigmoid_input[4] = {-2.0f, -0.5f, 0.5f, 2.0f};
  float sigmoid[4] = {0.0f};
  jog_sigmoid(sigmoid_input, sigmoid);
  for (int i = 0; i < 4; ++i) {
    const float expected_sigmoid =
        1.0f / (1.0f + expf(-sigmoid_input[i]));
    if (fabsf(sigmoid[i] - expected_sigmoid) > 1e-6f)
      return 3;
  }
  return 0;
}
