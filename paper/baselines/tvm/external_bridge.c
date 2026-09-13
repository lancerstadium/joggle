#include "external_bridge.h"

#include <stdint.h>
#include <stdlib.h>

#include "tvm/ffi/c_api.h"

typedef int32_t (*Packed)(void*, void*, int32_t, void*);

extern int32_t __tvm_ffi_conv2d(void*, void*, int32_t, void*);
extern int32_t __tvm_ffi_extrema(void*, void*, int32_t, void*);
extern int32_t __tvm_ffi_matmul(void*, void*, int32_t, void*);

static DLTensor tensor(float* data, int32_t rank, int64_t* shape) {
  DLTensor value = {0};
  value.data = data;
  value.device.device_type = kDLCPU;
  value.device.device_id = 0;
  value.ndim = rank;
  value.dtype.code = kDLFloat;
  value.dtype.bits = 32;
  value.dtype.lanes = 1;
  value.shape = shape;
  return value;
}

static TVMFFIAny tensor_arg(DLTensor* value) {
  TVMFFIAny argument = {0};
  argument.type_index = kTVMFFIDLTensorPtr;
  argument.v_ptr = value;
  return argument;
}

static TVMFFIAny int_arg(int64_t value) {
  TVMFFIAny argument = {0};
  argument.type_index = kTVMFFIInt;
  argument.v_int64 = value;
  return argument;
}

static void invoke(Packed function, TVMFFIAny* arguments, int32_t count) {
  TVMFFIAny result = {0};
  if (function(0, arguments, count, &result) != 0)
    abort();
}

static void call_matmul(const float* a, const float* b, int64_t rows,
                        int64_t columns, int64_t inner, float* out) {
  int64_t a_shape[] = {rows, inner};
  int64_t b_shape[] = {inner, columns};
  int64_t out_shape[] = {rows, columns};
  DLTensor tensors[] = {
      tensor((float*)a, 2, a_shape),
      tensor((float*)b, 2, b_shape),
      tensor(out, 2, out_shape),
  };
  TVMFFIAny arguments[] = {
      tensor_arg(&tensors[0]), tensor_arg(&tensors[1]), tensor_arg(&tensors[2]),
      int_arg(rows),           int_arg(columns),        int_arg(inner),
  };
  invoke(__tvm_ffi_matmul, arguments, 6);
}

void model_main(const float* a, const float* b, float* out) {
  call_matmul(a, b, 2, 2, 3, out);
}

void model_wide(const float* a, const float* b, float* out) {
  call_matmul(a, b, 1, 4, 3, out);
}

static void call_conv(const float* x, const float* weight, const float* bias,
                      int64_t has_bias, int64_t relu, float* out) {
  int64_t x_shape[] = {1, 1, 3, 3};
  int64_t weight_shape[] = {1, 1, 2, 2};
  int64_t bias_shape[] = {1};
  int64_t out_shape[] = {1, 1, 2, 2};
  DLTensor tensors[] = {
      tensor((float*)x, 4, x_shape),
      tensor((float*)weight, 4, weight_shape),
      tensor((float*)bias, 1, bias_shape),
      tensor(out, 4, out_shape),
  };
  TVMFFIAny arguments[23];
  for (int i = 0; i != 4; ++i)
    arguments[i] = tensor_arg(&tensors[i]);
  const int64_t values[] = {
      1, 1, 3, 3, 1, 1, 2, 2, 2, 2, 1, 1, 0, 0, 1, 1, 1, has_bias, relu,
  };
  for (int i = 0; i != 19; ++i)
    arguments[i + 4] = int_arg(values[i]);
  invoke(__tvm_ffi_conv2d, arguments, 23);
}

void model_conv(const float* x, const float* weight, float* out) {
  static const float zero[] = {0.0f};
  call_conv(x, weight, zero, 0, 0, out);
}

void model_fixed_conv(const float* x, float* out) {
  static const float weight[] = {1.0f, 0.0f, 0.0f, -1.0f};
  static const float zero[] = {0.0f};
  call_conv(x, weight, zero, 0, 0, out);
}

void model_biased_conv(const float* x, const float* weight, const float* bias,
                       float* out) {
  call_conv(x, weight, bias, 1, 1, out);
}

void model_extrema(const float* x, float* low, float* high) {
  int64_t input_shape[] = {4};
  int64_t output_shape[] = {1};
  DLTensor tensors[] = {
      tensor((float*)x, 1, input_shape),
      tensor(low, 1, output_shape),
      tensor(high, 1, output_shape),
  };
  TVMFFIAny arguments[] = {
      tensor_arg(&tensors[0]),
      tensor_arg(&tensors[1]),
      tensor_arg(&tensors[2]),
  };
  invoke(__tvm_ffi_extrema, arguments, 3);
}
