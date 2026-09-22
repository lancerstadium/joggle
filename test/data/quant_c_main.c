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

  /* The f32 product is exactly +/-84.5. Fusing the product with the
     subtraction inside round_even changes its tie test and yields 15/185. */
  const int32_t accumulators[4] = {-30531, 30531, 0, 1};
  const float tie_scale[1] = {0x1.6ac3e4p-9f};
  const uint8_t tie_zero[1] = {100};
  const uint8_t tie_expected[4] = {16, 184, 100, 100};
  uint8_t tie_actual[4] = {0};
  quant_kernel_requantize_ties(accumulators, tie_scale, tie_zero, tie_actual);
  for (size_t i = 0; i < 4; ++i)
    if (tie_actual[i] != tie_expected[i])
      return 3;
  /* The same value boundary is required for the f64 overload. */
  if (quant_kernel_rounded_product64(3.0, 0x1.c2aaaaaaaaaabp+4) != 84.0 ||
      quant_kernel_rounded_product64(-3.0, 0x1.c2aaaaaaaaaabp+4) != -84.0)
    return 4;
  return 0;
}
