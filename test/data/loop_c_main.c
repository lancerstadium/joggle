#include <stdbool.h>
#include <stdint.h>

void loop_c_scan(const int32_t* start, const bool* condition,
                 const int32_t* delta, const int32_t* limit,
                 int32_t* final, int32_t* history, int64_t* history_dim_0);

int main(void) {
  const int32_t start = 2, delta = 3;
  int32_t limit = 100, final = -1, history[4] = {-1, -1, -1, -1};
  bool condition = true;
  int64_t length = -1;
  loop_c_scan(&start, &condition, &delta, &limit, &final, history, &length);
  if (final != 14 || length != 4)
    return 1;
  for (int i = 0; i < 4; ++i)
    if (history[i] != 2 + 4 * i)
      return 2;
  limit = 8;
  loop_c_scan(&start, &condition, &delta, &limit, &final, history, &length);
  if (final != 8 || length != 2 || history[0] != 2 || history[1] != 6)
    return 3;
  condition = false;
  loop_c_scan(&start, &condition, &delta, &limit, &final, history, &length);
  if (final != start || length != 0)
    return 4;
  return 0;
}
