#include <math.h>

void jog_add(const float* a, const float* b, float* out);

int main(void) {
  const float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  const float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  const float expected[4] = {6.0f, 8.0f, 10.0f, 12.0f};
  float out[4] = {0.0f};
  jog_add(a, b, out);
  for (int i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  return 0;
}
