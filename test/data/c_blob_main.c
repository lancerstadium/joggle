#include "model-blob.h"

#include <stdint.h>

int main(void) {
  const unsigned char data[] = {
      0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40, 0xff, 0x00, 0x7f};
  float weights[2] = {0.0f, 0.0f};
  int8_t bytes[3] = {0, 0, 0};
  jog_weights(data, weights);
  jog_byte_weights(data, bytes);
  if (weights[0] != 1.0f || weights[1] != 2.0f)
    return 1;
  if (bytes[0] != -1 || bytes[1] != 0 || bytes[2] != 127)
    return 2;
  if (jog_direct(3.0f, data) != 6.0f)
    return 3;
  const float input[2] = {2.0f, 5.0f};
  if (jog_duplicate_sum(input, data) != 7.0f)
    return 4;
  return 0;
}
