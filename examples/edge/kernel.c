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
