#include <stdint.h>

int64_t tiled_sum(int64_t n);
int64_t tiled_grid(int64_t rows, int64_t cols);
int64_t tiled_fixed_grid(void);

static int64_t grid(int64_t rows, int64_t cols) {
  int64_t total = 0;
  for (int64_t row = 0; row < rows; ++row)
    for (int64_t column = 2; column < cols; ++column)
      if (column < cols - 1)
        total += row * 100 + column;
  return total;
}

int main(void) {
  if (tiled_sum(-3) != 0 || tiled_sum(0) != 0 || tiled_sum(1) != 0 ||
      tiled_sum(4) != 6 || tiled_sum(5) != 10 || tiled_sum(10) != 45 ||
      tiled_grid(-1, 7) != grid(-1, 7) ||
      tiled_grid(3, 1) != grid(3, 1) ||
      tiled_grid(3, 2) != grid(3, 2) ||
      tiled_grid(3, 3) != grid(3, 3) ||
      tiled_grid(3, 8) != grid(3, 8) ||
      tiled_fixed_grid() != INT64_C(12345678))
    return 1;
  return 0;
}
