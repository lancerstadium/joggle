void jog_edge_Dmatmul(const float* a, const float* b, float* out) {
  for (int i = 0; i != 2; ++i) {
    for (int j = 0; j != 2; ++j) {
      float sum = 0.0f;
      for (int k = 0; k != 3; ++k)
        sum += a[i * 3 + k] * b[k * 2 + j];
      out[i * 2 + j] = sum;
    }
  }
}
