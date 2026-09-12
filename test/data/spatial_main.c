#include <math.h>
#include <stddef.h>

void model_main(const float* x, const float* weight, float* out);

int main(void) {
  const float x[9] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
                      6.0f, 7.0f, 8.0f, 9.0f};
  const float weight[4] = {1.0f, 0.0f, 0.0f, -1.0f};
  float out[4] = {0.0f};
  model_main(x, weight, out);
  for (size_t i = 0; i < 4; ++i)
    if (fabsf(out[i] + 4.0f) > 1.0e-6f)
      return 1;
  return 0;
}
