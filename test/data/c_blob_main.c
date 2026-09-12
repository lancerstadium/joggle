#include "model-blob.h"

#include <stdint.h>

int main(void) {
  const union {
    double align;
    unsigned char data[12];
  } blob = {.data = {0xff, 0x00, 0x7f, 0x00, 0x00, 0x00,
                     0x80, 0x3f, 0x00, 0x00, 0x00, 0x40}};
  float weights[2] = {0.0f, 0.0f};
  int8_t bytes[3] = {0, 0, 0};
  kernel_weights(blob.data, weights);
  kernel_byte_weights(blob.data, bytes);
  if (weights[0] != 1.0f || weights[1] != 2.0f)
    return 1;
  if (bytes[0] != -1 || bytes[1] != 0 || bytes[2] != 127)
    return 2;
  if (kernel_direct(3.0f) != 6.0f)
    return 3;
  const float input[2] = {2.0f, 5.0f};
  if (kernel_duplicate_sum(input) != 7.0f)
    return 4;
  if (kernel_forwarded_weight(blob.data) != 2.0f)
    return 5;
  return 0;
}
