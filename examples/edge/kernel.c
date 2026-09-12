void jog_edge_matmul(const float* a, const float* b, float* out) {
  for (int i = 0; i != 2; ++i) {
    for (int j = 0; j != 2; ++j) {
      float sum = 0.0f;
      for (int k = 0; k != 3; ++k)
        sum += a[i * 3 + k] * b[k * 2 + j];
      out[i * 2 + j] = sum;
    }
  }
}

void jog_edge_extrema(const float* x, float* low, float* high) {
  *low = x[0];
  *high = x[0];
  for (int i = 1; i != 4; ++i) {
    if (x[i] < *low)
      *low = x[i];
    if (x[i] > *high)
      *high = x[i];
  }
}
