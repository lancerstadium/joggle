#include <stdint.h>

void nonzero_c_main(const int32_t* x, int64_t* result,
                    int64_t* result_dim_1);

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
  return 0;
}
