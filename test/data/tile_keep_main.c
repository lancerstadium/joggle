#include <math.h>

void jog_both(
    const float* a,
    const float* b,
    const float* c,
    float* first,
    float* second);

int main(void) {
  const float a[4] = {1.0f, -2.0f, 3.5f, 4.0f};
  const float b[4] = {5.0f, 6.0f, -1.5f, 8.0f};
  const float c[4] = {9.0f, -10.0f, 11.0f, 12.0f};
  float first[4] = {0.0f};
  float second[4] = {0.0f};
  jog_both(a, b, c, first, second);
  for (int i = 0; i < 4; ++i) {
    if (fabsf(first[i] - (a[i] + b[i])) > 1e-6f ||
        fabsf(second[i] - (a[i] + b[i] + c[i])) > 1e-6f)
      return 1;
  }
  return 0;
}
