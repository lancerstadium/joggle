#include <math.h>

void jog_add_relu(const float* a, const float* b, float* out);

int main(void) {
  const float a[6] = {-5.0f, -2.0f, 0.0f, 1.0f, 3.0f, 8.0f};
  const float b[6] = {1.0f, 2.0f, -1.0f, 4.0f, -7.0f, 0.5f};
  float out[6] = {0.0f};
  jog_add_relu(a, b, out);
  for (int i = 0; i < 6; ++i) {
    const float sum = a[i] + b[i];
    const float expected = sum > 0.0f ? sum : 0.0f;
    if (fabsf(out[i] - expected) > 1e-6f)
      return 1;
  }
  return 0;
}
