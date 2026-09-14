#include <math.h>

void tile_multi_chain(
    const float* a, const float* b, const float* c, float* out);
void tile_multi_reduce(const float* x, float* out);

int main(void) {
  const float a[6] = {-3.0f, -1.0f, 0.0f, 1.0f, 4.0f, 8.0f};
  const float b[6] = {2.0f, 3.0f, -5.0f, 7.0f, -2.0f, 0.5f};
  const float c[6] = {4.0f, -2.0f, 0.25f, 3.0f, 5.0f, -1.0f};
  float out[6] = {0.0f};
  tile_multi_chain(a, b, c, out);
  for (int i = 0; i < 6; ++i)
    if (fabsf(out[i] - (a[i] + b[i]) * c[i]) > 1e-6f)
      return 1;
  const float x[24] = {
      1.0f, 2.0f, 3.0f, 4.0f, -1.0f, -2.0f, -3.0f, -4.0f,
      0.5f, 1.5f, -0.5f, 2.5f, 7.0f, -2.0f, 1.0f, 3.0f,
      -5.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const float expected[6] = {11.0f, 0.0f, 5.0f, 10.0f, 0.0f, 0.0f};
  float reduced[6] = {0.0f};
  tile_multi_reduce(x, reduced);
  for (int i = 0; i < 6; ++i)
    if (fabsf(reduced[i] - expected[i]) > 1e-6f)
      return 2;
  return 0;
}
