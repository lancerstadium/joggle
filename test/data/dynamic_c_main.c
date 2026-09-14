#include <stdint.h>

void dynamic_c_prefix(int32_t* result, int64_t* result_dim_0);
int32_t dynamic_c_sum(const int32_t* x, int64_t x_dim_0);

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
  return 0;
}
