#include <stdint.h>
#include <stdbool.h>

void dynamic_c_prefix(int32_t* result, int64_t* result_dim_0);
void dynamic_c_static_result(const int32_t* x, int32_t* result, int64_t* length);
int32_t dynamic_c_sum(const int32_t* x, int64_t x_dim_0);
void dynamic_c_gather_last(const int32_t* x, int64_t x_dim_0, int32_t* result);
void dynamic_c_gather_middle(const int32_t* x, int64_t x_dim_1,
                             const int64_t* indices, int64_t indices_dim_0,
                             int32_t* result);
void dynamic_c_gather_scalar(const int32_t* x, int64_t x_dim_0, int32_t* result);
void dynamic_c_gather_i32(const int32_t* x, int64_t x_dim_0, int32_t* result);
void dynamic_c_shape(const int32_t* x, int64_t x_dim_0, int64_t* result);
void dynamic_c_updated(const int32_t* x, int32_t* result);
void dynamic_c_tile(const int32_t* x, const int64_t* repeats, int32_t* result);
void dynamic_c_tile_empty(const int32_t* x, const int64_t* repeats, int32_t* result);
void dynamic_c_tile_bounded(const int32_t* x, bool twice, int32_t* result,
                            int64_t* rows, int64_t* cols);

int main(int argc, char** argv) {
  if (argc == 2) {
    const int32_t input[6] = {0, 1, 2, 3, 4, 5};
    const int64_t invalid[3][2] = {{-1, 2}, {INT64_MAX, 2}, {1, 2}};
    const int selected = argv[1][0] - 'a';
    int32_t output[24] = {0};
    if (selected >= 0 && selected < 3)
      dynamic_c_tile(input, invalid[selected], output);
    return 0;
  }
  int32_t result[8] = {0};
  int64_t rows = -1;
  const int32_t fixed[3] = {7, 8, 9};
  dynamic_c_static_result(fixed, result, &rows);
  if (rows != 3 || result[0] != 7 || result[1] != 8 || result[2] != 9)
    return 15;
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
  dynamic_c_gather_i32(input, 3, gathered);
  if (gathered[0] != 4 || gathered[1] != 5)
    return 9;
  int32_t original[3] = {2, 3, 4}, updated[3] = {0};
  dynamic_c_updated(original, updated);
  if (original[0] != 2 || original[1] != 3 || original[2] != 4 ||
      updated[0] != 2 || updated[1] != 9 || updated[2] != 4)
    return 10;
  const int64_t repeats[2] = {2, 2}, empty_repeats[2] = {0, 2};
  int32_t tiled[24] = {0};
  dynamic_c_tile(input, repeats, tiled);
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 6; ++c)
      if (tiled[r * 6 + c] != input[(r % 2) * 3 + c % 3])
        return 11;
  tiled[0] = -1;
  dynamic_c_tile_empty(input, empty_repeats, tiled);
  if (tiled[0] != -1)
    return 12;
  for (int factor = 1; factor <= 2; ++factor) {
    int64_t tile_rows = -1, tile_cols = -1;
    dynamic_c_tile_bounded(input, factor == 2, tiled, &tile_rows, &tile_cols);
    if (tile_rows != 2 * factor || tile_cols != 3 * factor)
      return 13;
    for (int r = 0; r < tile_rows; ++r)
      for (int c = 0; c < tile_cols; ++c)
        if (tiled[r * tile_cols + c] != input[(r % 2) * 3 + c % 3])
          return 14;
  }
  return 0;
}
