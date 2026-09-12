#define _POSIX_C_SOURCE 200809L

#include "model-blob.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void* read(const char* path, size_t* bytes) {
  FILE* file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) {
    if (file)
      fclose(file);
    return NULL;
  }
  const long end = ftell(file);
  if (end <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  *bytes = (size_t)end;
  void* data = malloc(*bytes);
  if (!data || fread(data, 1, *bytes, file) != *bytes || fclose(file) != 0) {
    free(data);
    return NULL;
  }
  return data;
}

static int number(const char* text, size_t* value) {
  char* end = NULL;
  errno = 0;
  if (*text == '-')
    return 0;
  const unsigned long long parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' ||
      parsed > (unsigned long long)SIZE_MAX)
    return 0;
  *value = (size_t)parsed;
  return 1;
}

static double now(void) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
    return -1.0;
  return (double)time.tv_sec + (double)time.tv_nsec * 1.0e-9;
}

int main(int argc, char** argv) {
  if (argc != 6)
    return 2;

  size_t output_count = 0;
  size_t warmup = 0;
  size_t repetitions = 0;
  if (!number(argv[3], &output_count) || output_count == 0 ||
      output_count > SIZE_MAX / sizeof(float) ||
      !number(argv[4], &warmup) || !number(argv[5], &repetitions) ||
      repetitions == 0)
    return 3;

  size_t input_bytes = 0;
  size_t data_bytes = 0;
  float* input = read(argv[1], &input_bytes);
  unsigned char* data = read(argv[2], &data_bytes);
  float* output = calloc(output_count, sizeof(float));
  if (!input || !data || !output || input_bytes % sizeof(float) != 0 ||
      data_bytes == 0) {
    free(output);
    free(data);
    free(input);
    return 4;
  }

  for (size_t i = 0; i < warmup; ++i)
    jog_main(input, data, output);

  puts("iteration,seconds,checksum");
  for (size_t i = 0; i < repetitions; ++i) {
    const double begin = now();
    jog_main(input, data, output);
    const double end = now();
    if (begin < 0.0 || end < begin) {
      free(output);
      free(data);
      free(input);
      return 5;
    }
    double checksum = 0.0;
    for (size_t j = 0; j < output_count; ++j) {
      if (!isfinite(output[j])) {
        free(output);
        free(data);
        free(input);
        return 6;
      }
      checksum += output[j];
    }
    if (!isfinite(checksum)) {
      free(output);
      free(data);
      free(input);
      return 6;
    }
    printf("%zu,%.9f,%.17g\n", i, end - begin, checksum);
  }

  free(output);
  free(data);
  free(input);
  return 0;
}
