#include <stdint.h>

void topk_c_largest(float* result);
void topk_c_indices(int64_t* result);
void topk_c_smallest(float* result);

int main(void) {
  const float largest_expected[4] = {7.0f, 7.0f, 9.0f, 5.0f};
  const int64_t indices_expected[4] = {1, 2, 2, 3};
  const float smallest_expected[4] = {-1.0f, 3.0f, 2.0f, 4.0f};
  float largest[4] = {0.0f};
  int64_t indices[4] = {0};
  float smallest[4] = {0.0f};
  topk_c_largest(largest);
  topk_c_indices(indices);
  topk_c_smallest(smallest);
  for (int i = 0; i < 4; ++i) {
    if (largest[i] != largest_expected[i])
      return 1;
    if (indices[i] != indices_expected[i])
      return 2;
    if (smallest[i] != smallest_expected[i])
      return 3;
  }
  return 0;
}
