#include <stdint.h>

void edge_matmul(const float* a, const float* b, int64_t rows,
                 int64_t columns, int64_t inner, float* out) {
  for (int64_t i = 0; i != rows; ++i) {
    for (int64_t j = 0; j != columns; ++j) {
      float sum = 0.0f;
      for (int64_t k = 0; k != inner; ++k)
        sum += a[i * inner + k] * b[k * columns + j];
      out[i * columns + j] = sum;
    }
  }
}

void edge_conv2d(const float* x, const float* weight, int64_t batch,
                 int64_t height, int64_t width, int64_t outputs,
                 int64_t group_channels,
                 int64_t kernel_height, int64_t kernel_width,
                 int64_t output_height, int64_t output_width,
                 int64_t stride_height, int64_t stride_width,
                 int64_t pad_top, int64_t pad_left,
                 int64_t dilation_height, int64_t dilation_width,
                 int64_t groups, int64_t x_n_stride, int64_t x_c_stride,
                 int64_t x_h_stride, int64_t x_w_stride,
                 int64_t weight_m_stride, int64_t weight_q_stride,
                 int64_t weight_r_stride, int64_t weight_s_stride,
                 int64_t out_n_stride, int64_t out_m_stride,
                 int64_t out_h_stride, int64_t out_w_stride, float* out) {
  const int64_t outputs_per_group = outputs / groups;
  for (int64_t n = 0; n != batch; ++n) {
    for (int64_t m = 0; m != outputs; ++m) {
      const int64_t channel_base = (m / outputs_per_group) * group_channels;
      for (int64_t oh = 0; oh != output_height; ++oh) {
        for (int64_t ow = 0; ow != output_width; ++ow) {
          float sum = 0.0f;
          for (int64_t q = 0; q != group_channels; ++q) {
            for (int64_t r = 0; r != kernel_height; ++r) {
              const int64_t h = oh * stride_height + r * dilation_height -
                                pad_top;
              if (h < 0 || h >= height)
                continue;
              for (int64_t s = 0; s != kernel_width; ++s) {
                const int64_t w = ow * stride_width + s * dilation_width -
                                  pad_left;
                if (w < 0 || w >= width)
                  continue;
                const int64_t input_index = n * x_n_stride +
                                            (channel_base + q) * x_c_stride +
                                            h * x_h_stride + w * x_w_stride;
                const int64_t weight_index = m * weight_m_stride +
                                             q * weight_q_stride +
                                             r * weight_r_stride +
                                             s * weight_s_stride;
                sum += x[input_index] * weight[weight_index];
              }
            }
          }
          out[n * out_n_stride + m * out_m_stride + oh * out_h_stride +
              ow * out_w_stride] = sum;
        }
      }
    }
  }
}

void edge_extrema(const float* x, float* low, float* high) {
  *low = x[0];
  *high = x[0];
  for (int i = 1; i != 4; ++i) {
    if (x[i] < *low)
      *low = x[i];
    if (x[i] > *high)
      *high = x[i];
  }
}
