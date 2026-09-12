#include <math.h>
#include <fenv.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
  float weights[2] = {0.0f, 0.0f};
  kernel_weights(weights);
  if (weights[0] != 1.0f || weights[1] != 2.0f)
    return 7;
  int8_t byte_weights[3] = {0};
  kernel_byte_weights(byte_weights);
  if (byte_weights[0] != -1 || byte_weights[1] != 0 ||
      byte_weights[2] != 127)
    return 8;

  const int64_t integers_a[6] = {1, 2, 3, 4, 5, 6};
  const int64_t integers_b[6] = {7, 8, 9, 10, 11, 12};
  const int64_t integers_expected[6] = {8, 10, 12, 14, 16, 18};
  int64_t integers_out[6] = {0};
  kernel_int_add(integers_a, integers_b, integers_out);
  for (size_t i = 0; i < 6; ++i)
    if (integers_out[i] != integers_expected[i])
      return 5;
  const int64_t product_expected[4] = {58, 64, 139, 154};
  int64_t product[4] = {0};
  kernel_int_matmul(integers_a, integers_b, product);
  for (size_t i = 0; i < 4; ++i)
    if (product[i] != product_expected[i])
      return 6;
  const int64_t transpose_expected[6] = {1, 4, 2, 5, 3, 6};
  int64_t transposed[6] = {0};
  kernel_transpose(integers_a, transposed);
  for (size_t i = 0; i < 6; ++i)
    if (transposed[i] != transpose_expected[i])
      return 25;
  const float scalar[1] = {2.5f};
  float broadcast[6] = {0.0f};
  kernel_broadcast_scalar(scalar, broadcast);
  for (size_t i = 0; i < 6; ++i)
    if (broadcast[i] != 2.5f)
      return 26;
  const float identity_input[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  float identity[6] = {0.0f};
  kernel_broadcast_identity(identity_input, identity);
  for (size_t i = 0; i < 6; ++i)
    if (identity[i] != identity_input[i])
      return 28;
  const float extrema_left[2] = {NAN, -4.0f};
  const float extrema_right[3] = {1.0f, 3.0f, -5.0f};
  const float maximum_expected[3] = {1.0f, 3.0f, -4.0f};
  const float minimum_expected[3] = {-4.0f, -4.0f, -5.0f};
  float maximum[6] = {0.0f};
  float minimum[6] = {0.0f};
  kernel_extrema(extrema_left, extrema_right, maximum, minimum);
  for (size_t i = 0; i < 3; ++i)
    if (!isnan(maximum[i]) || !isnan(minimum[i]))
      return 29;
  for (size_t i = 0; i < 3; ++i)
    if (maximum[i + 3] != maximum_expected[i] ||
        minimum[i + 3] != minimum_expected[i])
      return 30;
  if (!kernel_list_branch(2) || kernel_list_branch(1))
    return 27;

  const float a[6] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
  const float b[6] = {7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f};
  const float expected[4] = {58.0f, 64.0f, 139.0f, 154.0f};
  float out[4] = {0.0f};
  kernel_matmul(a, b, out);
  for (size_t i = 0; i < 4; ++i)
    if (fabsf(out[i] - expected[i]) > 1e-6f)
      return 1;
  if (fabsf(affine_kernel(3.0f) - 7.0f) > 1e-6f)
    return 2;
  if (fabsf(kernel_direct(3.0f) - 6.0f) > 1e-6f)
    return 3;
  if (fabsf(kernel_relu(-2.0f)) > 1e-6f ||
      fabsf(kernel_relu(3.0f) - 3.0f) > 1e-6f)
    return 4;
  if (!kernel_logical(2, 1) || kernel_logical(2, -1) ||
      kernel_logical(-2, -2))
    return 8;
  if (fabsf(kernel_root(9.0f) - 4.0f) > 1e-6f)
    return 9;
  const double unary_expected =
      fabs(1.5) + floor(1.5) + log(1.5) + erf(1.5) + exp(1.5) +
      ceil(1.5) + tanh(1.5) + nearbyint(1.5);
  if (fabs(kernel_unary_math(1.5) - unary_expected) > 1e-12)
    return 12;
  if (kernel_power(2.0, 5.0) != 32.0)
    return 13;
  if (fesetround(FE_UPWARD) != 0)
    return 16;
  const bool fixed_rounding = kernel_nearest(2.5) == 2.0 &&
                              kernel_nearest(1.5) == 2.0 &&
                              kernel_nearest(-0.5) == 0.0;
  if (fesetround(FE_TONEAREST) != 0 || !fixed_rounding)
    return 17;
  int64_t quotient = 0;
  int64_t remainder = 0;
  kernel_split(9, &quotient, &remainder);
  if (quotient != 4 || remainder != 1 || kernel_recombine(9) != 9)
    return 18;
  const float duplicate_input[2] = {2.0f, 5.0f};
  float duplicate_output[2] = {0.0f, 0.0f};
  float duplicate_first = 0.0f;
  kernel_duplicate(duplicate_input, duplicate_output, &duplicate_first);
  if (duplicate_output[0] != 2.0f || duplicate_output[1] != 5.0f ||
      duplicate_first != 2.0f || kernel_duplicate_sum(duplicate_input) != 7.0f)
    return 19;
  if (kernel_call_noop() != 1)
    return 20;
  if (kernel_abi_probe(2, 3, 4) != 9)
    return 15;
  if (kernel_steps() != 3)
    return 10;
  if (kernel_select() != 7)
    return 11;
  if (kernel_captured_steps() != 6)
    return 24;
  if (!kernel_assign_literal())
    return 14;
  return 0;
}
