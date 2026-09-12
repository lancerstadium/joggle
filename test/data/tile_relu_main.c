#include <math.h>

void tile_relu_add_relu(const float* a, const float* b, float* out);
void tile_relu_batch_relu(
    const float* x,
    const float* scale,
    const float* bias,
    const float* mean,
    const float* variance,
    float* out);

int main(void) {
  const float a[6] = {-5.0f, -2.0f, 0.0f, 1.0f, 3.0f, 8.0f};
  const float b[6] = {1.0f, 2.0f, -1.0f, 4.0f, -7.0f, 0.5f};
  float out[6] = {0.0f};
  tile_relu_add_relu(a, b, out);
  for (int i = 0; i < 6; ++i) {
    const float sum = a[i] + b[i];
    const float expected = sum > 0.0f ? sum : 0.0f;
    if (fabsf(out[i] - expected) > 1e-6f)
      return 1;
  }
  const float x[8] = {-3.0f, -1.0f, 1.0f, 3.0f,
                      -4.0f, 0.0f, 4.0f, 8.0f};
  const float scale[2] = {2.0f, 0.5f};
  const float bias[2] = {1.0f, -1.0f};
  const float mean[2] = {1.0f, 2.0f};
  const float variance[2] = {4.0f, 1.0f};
  float batch[8] = {0.0f};
  tile_relu_batch_relu(x, scale, bias, mean, variance, batch);
  for (int i = 0; i < 8; ++i) {
    const int channel = i / 4;
    const float normalized =
        (x[i] - mean[channel]) / sqrtf(variance[channel]) * scale[channel] +
        bias[channel];
    const float expected = normalized > 0.0f ? normalized : 0.0f;
    if (fabsf(batch[i] - expected) > 1e-6f)
      return 2;
  }
  return 0;
}
