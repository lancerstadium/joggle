#include <math.h>
#include <stddef.h>

void jog_matmul(const float* a, const float* b, float* out);
float jog_affine(float x);
float jog_relu(float x);

int main(void) {
  const float a[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  const float b[6] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  const float expected[4] = {58.0f, 64.0f, 139.0f, 154.0f};
  float out[4] = {0.0f};
  jog_matmul(a, b, out);
  for (size_t i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  if (fabsf(jog_affine(3.0f) - 7.0f) > 1e-6f)
    return 2;
  if (fabsf(jog_relu(-2.0f)) > 1e-6f ||
      fabsf(jog_relu(3.0f) - 3.0f) > 1e-6f)
    return 3;
  return 0;
}
