#include <math.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
  const uint8_t quantized[2] = {10, 14};
  const float scale[1] = {0.5f};
  const uint8_t zero[1] = {10};
  const float expected[2] = {0.0f, 2.0f};
  float dequantized[2] = {0.0f, 0.0f};
  quant_kernel_dequant_scalar(quantized, scale, zero, dequantized);
  for (size_t i = 0; i < 2; ++i)
    if (fabsf(dequantized[i] - expected[i]) > 1e-6f)
      return 1;

  uint8_t requantized[2] = {0, 0};
  quant_kernel_quant_scalar(expected, scale, zero, requantized);
  if (requantized[0] != 10 || requantized[1] != 14)
    return 2;
  return 0;
}
