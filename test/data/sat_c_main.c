#include <stdint.h>

int main(void) {
  if (jog_add5(15, 15) != 15 || jog_add5(-16, -1) != -16)
    return 1;
  if (jog_add8(120, 20) != 127 || jog_add8(-120, -20) != -128)
    return 2;
  if (jog_add12(2000, 2000) != 2047 ||
      jog_add12(-2000, -2000) != -2048)
    return 3;
  const int8_t a[4] = {15, 10, -16, -10};
  const int8_t b[4] = {1, 10, -1, -10};
  const int8_t expected[4] = {15, 15, -16, -16};
  int8_t out[4] = {0, 0, 0, 0};
  jog_add_vec5(a, b, out);
  for (int i = 0; i < 4; ++i)
    if (out[i] != expected[i])
      return 4;
  return 0;
}
