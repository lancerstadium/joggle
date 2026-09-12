#include <math.h>
#include <fenv.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

const unsigned char jog_data_weights[] = {
    0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40,
    0xff, 0x00, 0x7f,
};

int main(void) {
  float weights[2] = {0.0f, 0.0f};
  jog_weights(weights);
  if (weights[0] != 1.0f || weights[1] != 2.0f)
    return 7;
  int8_t byte_weights[3] = {0};
  jog_byte_weights(byte_weights);
  if (byte_weights[0] != -1 || byte_weights[1] != 0 ||
      byte_weights[2] != 127)
    return 8;

  const int64_t integers_a[6] = {1, 2, 3, 4, 5, 6};
  const int64_t integers_b[6] = {7, 8, 9, 10, 11, 12};
  const int64_t integers_expected[6] = {8, 10, 12, 14, 16, 18};
  int64_t integers_out[6] = {0};
  jog_int_add(integers_a, integers_b, integers_out);
  for (size_t i = 0; i < 6; ++i)
    if (integers_out[i] != integers_expected[i])
      return 5;
  const int64_t product_expected[4] = {58, 64, 139, 154};
  int64_t product[4] = {0};
  jog_int_matmul(integers_a, integers_b, product);
  for (size_t i = 0; i < 4; ++i)
    if (product[i] != product_expected[i])
      return 6;

  const float a[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  const float b[6] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  const float expected[4] = {58.0f, 64.0f, 139.0f, 154.0f};
  float out[4] = {0.0f};
  jog_matmul(a, b, out);
  for (size_t i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  if (fabsf(kernel_affine(3.0f) - 7.0f) > 1e-6f)
    return 2;
  if (fabsf(jog_direct(3.0f) - 6.0f) > 1e-6f)
    return 3;
  if (fabsf(jog_relu(-2.0f)) > 1e-6f ||
      fabsf(jog_relu(3.0f) - 3.0f) > 1e-6f)
    return 4;
  if (!jog_logical(2, 1) || jog_logical(2, -1) ||
      jog_logical(-2, -2))
    return 8;
  if (fabsf(jog_root(9.0f) - 4.0f) > 1e-6f)
    return 9;
  const double unary_expected =
      fabs(1.5) + floor(1.5) + log(1.5) + erf(1.5) + exp(1.5) +
      ceil(1.5) + tanh(1.5) + nearbyint(1.5);
  if (fabs(jog_unary_math(1.5) - unary_expected) > 1e-12)
    return 12;
  if (jog_power(2.0, 5.0) != 32.0)
    return 13;
  if (fesetround(FE_UPWARD) != 0)
    return 16;
  const bool fixed_rounding = jog_nearest(2.5) == 2.0 &&
                              jog_nearest(1.5) == 2.0 &&
                              jog_nearest(-0.5) == 0.0;
  if (fesetround(FE_TONEAREST) != 0 || !fixed_rounding)
    return 17;
  int64_t quotient = 0;
  int64_t remainder = 0;
  jog_split(9, &quotient, &remainder);
  if (quotient != 4 || remainder != 1 || jog_recombine(9) != 9)
    return 18;
  const float duplicate_input[2] = {2.0f, 5.0f};
  float duplicate_output[2] = {0.0f, 0.0f};
  float duplicate_first = 0.0f;
  jog_duplicate(duplicate_input, duplicate_output, &duplicate_first);
  if (duplicate_output[0] != 2.0f || duplicate_output[1] != 5.0f ||
      duplicate_first != 2.0f || jog_duplicate_sum(duplicate_input) != 7.0f)
    return 19;
  if (jog_call_noop() != 1)
    return 20;
  if (jog_abi_probe(2, 3, 4) != 9)
    return 15;
  if (jog_steps() != 3)
    return 10;
  if (jog_select() != 7)
    return 11;
  if (!jog_assign_literal())
    return 14;
  return 0;
}
