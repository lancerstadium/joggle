#include <stdint.h>

void dynamic_c_prefix(int32_t* result, int64_t* result_dim_0);
int32_t dynamic_c_sum(const int32_t* x, int64_t x_dim_0);
void dynamic_c_gather_last(const int32_t* x, int64_t x_dim_0, int32_t* result);
void dynamic_c_gather_middle(const int32_t* x, int64_t x_dim_1,
                             const int64_t* indices, int64_t indices_dim_0,
                             int32_t* result);
void dynamic_c_gather_scalar(const int32_t* x, int64_t x_dim_0, int32_t* result);
void dynamic_c_shape(const int32_t* x, int64_t x_dim_0, int64_t* result);

int main(void) {
  int32_t result[8] = {0};
  int64_t rows = -1;
  dynamic_c_prefix(result, &rows);
  if (rows != 3)
    return 1;
  for (int64_t i = 0; i < rows; ++i)
    for (int64_t j = 0; j < 2; ++j)
      if (result[i * 2 + j] != 7)
        return 2;
  if (dynamic_c_sum(result, rows) != 42)
    return 3;
  const int32_t input[6] = {0, 1, 2, 3, 4, 5};
  int32_t gathered[2] = {-1, -1};
  dynamic_c_gather_last(input, 3, gathered);
  if (gathered[0] != 4 || gathered[1] != 5)
    return 4;
  int64_t shape[2] = {-1, -1};
  dynamic_c_shape(input, 3, shape);
  if (shape[0] != 3 || shape[1] != 2)
    return 5;
  const int32_t volume[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  const int64_t indices[2] = {-1, 0};
  const int32_t expected[8] = {4, 5, 0, 1, 10, 11, 6, 7};
  int32_t middle[8] = {0};
  dynamic_c_gather_middle(volume, 3, indices, 2, middle);
  for (int i = 0; i < 8; ++i)
    if (middle[i] != expected[i])
      return 6;
  dynamic_c_gather_scalar(input, 3, gathered);
  if (gathered[0] != 4 || gathered[1] != 5)
    return 7;
  dynamic_c_shape(input, 0, shape);
  if (shape[0] != 0 || shape[1] != 2)
    return 8;
  return 0;
}
