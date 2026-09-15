#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "OnnxMlirRuntime.h"

void evolution_conv(const float *x, const float *weight, int64_t batch,
                    int64_t height, int64_t width, int64_t outputs,
                    int64_t group_channels, int64_t kernel_height,
                    int64_t kernel_width, int64_t output_height,
                    int64_t output_width, int64_t stride_height,
                    int64_t stride_width, int64_t pad_top, int64_t pad_left,
                    int64_t dilation_height, int64_t dilation_width,
                    int64_t groups, int64_t x_n_stride, int64_t x_c_stride,
                    int64_t x_h_stride, int64_t x_w_stride,
                    int64_t weight_m_stride, int64_t weight_q_stride,
                    int64_t weight_r_stride, int64_t weight_s_stride,
                    int64_t out_n_stride, int64_t out_m_stride,
                    int64_t out_h_stride, int64_t out_w_stride, float *out);

/* The signature is the verified krnl.call -> LLVM declaration for this task.
   OMTensor arguments are output, activation, and constant weight. ONNX scalar
   and integer-list attributes follow in the emitted order. This bridge owns no
   model semantics; the portable Add and ReLU remain in ONNX-MLIR's artifact. */
void Conv(OMTensor *out, OMTensor *x, OMTensor *weight,
          const char *auto_pad, int64_t group, const char *node_name,
          int64_t pad_top, int64_t pad_left, int64_t pad_bottom,
          int64_t pad_right, int64_t stride_height, int64_t stride_width) {
  (void)node_name;
  if (!out || !x || !weight || !auto_pad || strcmp(auto_pad, "NOTSET") != 0 ||
      omTensorGetRank(out) != 4 || omTensorGetRank(x) != 4 ||
      omTensorGetRank(weight) != 4 || group <= 0 ||
      pad_top != pad_bottom || pad_left != pad_right ||
      omTensorGetDataType(out) != ONNX_TYPE_FLOAT ||
      omTensorGetDataType(x) != ONNX_TYPE_FLOAT ||
      omTensorGetDataType(weight) != ONNX_TYPE_FLOAT)
    abort();

  const int64_t *xs = omTensorGetShape(x);
  const int64_t *ws = omTensorGetShape(weight);
  const int64_t *ys = omTensorGetShape(out);
  const int64_t *xt = omTensorGetStrides(x);
  const int64_t *wt = omTensorGetStrides(weight);
  const int64_t *yt = omTensorGetStrides(out);
  if (!xs || !ws || !ys || !xt || !wt || !yt ||
      ys[0] != xs[0] || ys[1] != ws[0] || ws[1] != xs[1] / group)
    abort();

  evolution_conv((const float *)omTensorGetDataPtr(x),
                 (const float *)omTensorGetDataPtr(weight), xs[0], xs[2], xs[3],
                 ws[0], ws[1], ws[2], ws[3], ys[2], ys[3], stride_height,
                 stride_width, pad_top, pad_left, 1, 1, group,
                 xt[0], xt[1], xt[2], xt[3], wt[0], wt[1], wt[2], wt[3],
                 yt[0], yt[1], yt[2], yt[3],
                 (float *)omTensorGetDataPtr(out));
}
