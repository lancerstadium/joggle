#include <stdint.h>
#include <stdbool.h>

void topk_c_largest(float* result);
void topk_c_indices(int64_t* result);
void topk_c_smallest(float* result);
void topk_c_dynamic(int64_t rows, int64_t count, int64_t axis, bool largest,
                    float* result);
void topk_c_dynamic_vector(int64_t extent, int64_t count, float* result);

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
  const float expected[4][10] = {
    {2, 2, 7, 7, 9, 5, 1, 2, 2, 3},
    {2, 2, -1, 3, 2, 4, 3, 0, 1, 0},
    {1, 4, 4, 7, 9, 5, 1, 0, 1, 1},
    {1, 3, 7, 7, 3, -99, 1, 2, 0, -99}
  };
  for (int trial = 0; trial < 4; ++trial) {
    float result[18];
    topk_c_dynamic(trial == 3 ? 1 : 2,
                   trial == 2 ? 1 : trial == 3 ? 3 : 2,
                   trial == 2 ? 0 : 1, trial != 1, result);
    if (result[0] != expected[trial][0] || result[1] != expected[trial][1])
      return 4;
    for (int i = 0; i < 4; ++i) {
      if (result[2 + i] != expected[trial][2 + i] ||
          result[10 + i] != expected[trial][6 + i])
        return 5;
    }
  }
  float vector[8];
  const float vector_expected[8] = {3, 7, 7, 4, -99, 1, 2, 4};
  topk_c_dynamic_vector(6, 3, vector);
  for (int i = 0; i < 8; ++i)
    if (vector[i] != vector_expected[i])
      return 6;
  topk_c_dynamic_vector(8, 1, vector);
  if (vector[0] != 1 || vector[1] != 9 || vector[5] != 6)
    return 7;
  return 0;
}
