#include <math.h>

void chain_add3(
    const float* a,
    const float* b,
    const float* c,
    const float* d,
    float* out);

int main(void) {
  const float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  const float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  const float c[4] = {9.0f, 10.0f, 11.0f, 12.0f};
  const float d[4] = {13.0f, 14.0f, 15.0f, 16.0f};
  const float expected[4] = {28.0f, 32.0f, 36.0f, 40.0f};
  float out[4] = {0.0f};
  chain_add3(a, b, c, d, out);
  for (int i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  return 0;
}
