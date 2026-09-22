#include <stdbool.h>
#include <stdint.h>

void loop_c_scan(const int32_t* start, const bool* condition,
                 const int32_t* delta, const int32_t* limit,
                 int32_t* final, int32_t* history, int64_t* history_dim_0);
void loop_c_reshape(const int32_t* x, const int64_t* requested,
                    int32_t* result, int64_t* dim_0, int64_t* dim_1);
void loop_c_reshape_empty(const int32_t* x, const int64_t* requested,
                          int32_t* result, int64_t* dim_0, int64_t* dim_1);
void loop_c_reshape_rank(const int32_t* x, const int64_t* requested,
                         int32_t* result, int64_t* dim_0, int64_t* dim_1,
                         int64_t* dim_2);
void loop_c_shape_pipeline(const int32_t* x, bool full, int32_t* result,
                           int64_t* rows);

int main(int argc, char** argv) {
  if (argc == 2) {
    const int32_t input[6] = {1, 2, 3, 4, 5, 6};
    int32_t result[6] = {0};
    int64_t rows = 0, cols = 0;
    const int64_t invalid[6][2] = {
        {-1, -1}, {4, 2}, {-1, 4}, {INT64_MAX, 2}, {-2, 3}, {0, -1}};
    const int selected = argv[1][0] - 'a';
    if (selected < 0 || selected >= 6)
      return 0;
    if (selected == 5)
      loop_c_reshape_empty(input, invalid[selected], result, &rows, &cols);
    else
      loop_c_reshape(input, invalid[selected], result, &rows, &cols);
    return 0;  // Every invalid shape must be rejected before a data access.
  }
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
  const int32_t input[6] = {1, 2, 3, 4, 5, 6};
  int32_t result[6] = {0};
  int64_t rows = -1, cols = -1;
  const int64_t shapes[3][2] = {{3, 2}, {-1, 2}, {0, -1}};
  for (int n = 0; n < 3; ++n) {
    loop_c_reshape(input, shapes[n], result, &rows, &cols);
    if (rows != (n == 2 ? 2 : 3) || cols != (n == 2 ? 3 : 2))
      return 5;
    for (int i = 0; i < 6; ++i)
      if (result[i] != input[i])
        return 6;
  }
  const int64_t empty[2] = {3, 0};
  loop_c_reshape_empty(input, empty, result, &rows, &cols);
  if (rows != 3 || cols != 0)
    return 7;
  const int64_t ranked[3] = {1, 2, -1};
  int64_t depth = -1;
  loop_c_reshape_rank(input, ranked, result, &depth, &rows, &cols);
  if (depth != 1 || rows != 2 || cols != 3)
    return 8;
  for (int i = 0; i < 6; ++i)
    if (result[i] != input[i])
      return 9;
  const int32_t vector[4] = {2, 4, 6, 8};
  int32_t pipeline[24] = {0};
  for (int full = 0; full < 2; ++full) {
    loop_c_shape_pipeline(vector, full != 0, pipeline, &rows);
    if (rows != (full ? 4 : 2))
      return 10;
    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < 6; ++c)
        if (pipeline[r * 6 + c] != vector[r])
          return 11;
  }
  return 0;
}
