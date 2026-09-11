#include <stdint.h>

int8_t jog_add5(int8_t a, int8_t b);
int8_t jog_add8(int8_t a, int8_t b);
int16_t jog_add12(int16_t a, int16_t b);

int main(void) {
  if (jog_add5(15, 15) != 15 || jog_add5(-16, -1) != -16)
    return 1;
  if (jog_add8(120, 20) != 127 || jog_add8(-120, -20) != -128)
    return 2;
  if (jog_add12(2000, 2000) != 2047 ||
      jog_add12(-2000, -2000) != -2048)
    return 3;
  return 0;
}
