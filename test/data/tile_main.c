#include <stdint.h>

int64_t jog_sum(int64_t n);
int64_t jog_grid(int64_t rows, int64_t cols);

static int64_t grid(int64_t rows, int64_t cols) {
  int64_t total = 0;
  for (int64_t row = 0; row < rows; ++row)
    for (int64_t column = 2; column < cols; ++column)
      if (column < cols - 1)
        total += row * 100 + column;
  return total;
}

int main(void) {
  if (jog_sum(-3) != 0 || jog_sum(0) != 0 || jog_sum(1) != 0 ||
      jog_sum(4) != 6 || jog_sum(5) != 10 || jog_sum(10) != 45 ||
      jog_grid(-1, 7) != grid(-1, 7) ||
      jog_grid(3, 1) != grid(3, 1) ||
      jog_grid(3, 2) != grid(3, 2) ||
      jog_grid(3, 3) != grid(3, 3) ||
      jog_grid(3, 8) != grid(3, 8))
    return 1;
  return 0;
}
