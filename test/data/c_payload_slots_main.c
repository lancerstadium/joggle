#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  union {
    double alignment;
    unsigned char bytes[128];
  } data = {0};
  unsigned char saved[128];
  if (argc != 2)
    return 1;
  FILE *file = fopen(argv[1], "rb");
  if (!file)
    return 2;
  size_t count = fread(data.bytes, 1, sizeof(data.bytes), file);
  int failed = ferror(file);
  fclose(file);
  if (failed || count == 0 || count == sizeof(data.bytes))
    return 3;
  memcpy(saved, data.bytes, sizeof(saved));
  for (int repeat = 0; repeat < 3; ++repeat) {
    if (payload_slots_mutable(7, data.bytes) != 17 ||
        payload_slots_mutable(11, data.bytes) != 21 ||
        payload_slots_immutable(data.bytes) != 3)
      return 4;
    if (memcmp(saved, data.bytes, sizeof(saved)) != 0)
      return 5;
  }
  return 0;
}
