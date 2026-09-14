#include <stdint.h>

int32_t tile_merge_case_checksum(const int32_t x[static 24]);

int main(void) {
  int32_t values[24];
  for (int32_t i = 0; i < 24; ++i)
    values[i] = i + 1;
  return tile_merge_case_checksum(values) == 162 ? 0 : 1;
}
