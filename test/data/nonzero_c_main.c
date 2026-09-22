#include <stdint.h>

void nonzero_c_main(const int32_t* x, int64_t* result,
                    int64_t* result_dim_1);
void nonzero_c_filtered(const float* x, int64_t count, int64_t* result,
                        int64_t* result_dim_1);
void nonzero_c_rows(const int32_t* x, int64_t rows, int64_t* result,
                    int64_t* result_dim_1);
void nonzero_c_coordinates(const float* x, int64_t count, int64_t* result,
                           int64_t* result_dim_0);

int main(void) {
  const int32_t input[6] = {0, 1, 0, 2, 3, 0};
  const int64_t expected[6] = {0, 1, 1, 1, 0, 1};
  int64_t result[12] = {0};
  int64_t selected = -1;
  nonzero_c_main(input, result, &selected);
  if (selected != 3)
    return 1;
  for (int64_t i = 0; i < 2 * selected; ++i)
    if (result[i] != expected[i])
      return 2;
  const float values[4] = {-1, 2, 0, 3};
  for (int64_t count = 0; count <= 4; ++count) {
    selected = -1;
    result[0] = -1;
    nonzero_c_filtered(values, count, result, &selected);
    const int64_t wanted = count < 2 ? 0 : count < 4 ? 1 : 2;
    if (selected != wanted) return 3;
    if (wanted == 0 && result[0] != -1) return 4;
    if (wanted >= 1 && result[0] != 1) return 5;
    if (wanted == 2 && result[1] != 3) return 6;
    result[0] = -1;
    nonzero_c_coordinates(values, count, result, &selected);
    if (selected != wanted) return 10;
    if (wanted == 0 && result[0] != -1) return 11;
    if (wanted >= 1 && result[0] != 1) return 12;
    if (wanted == 2 && result[1] != 3) return 13;
  }
  for (int64_t rows = 0; rows <= 2; ++rows) {
    nonzero_c_rows(input, rows, result, &selected);
    if (selected != (rows == 0 ? 0 : rows == 1 ? 1 : 3)) return 7;
    if (rows == 1 && (result[0] != 0 || result[1] != 1)) return 8;
    if (rows == 2)
      for (int64_t i = 0; i < 2 * selected; ++i)
        if (result[i] != expected[i]) return 9;
  }
  return 0;
}
