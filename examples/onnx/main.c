#include "model.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;
  size_t input_bytes = 0;
  size_t output_bytes = 0;
  float* input = read(argv[1], &input_bytes);
  float* expected = read(argv[2], &output_bytes);
  if (!input || !expected || input_bytes % sizeof(float) != 0 ||
      output_bytes % sizeof(float) != 0) {
    free(expected);
    free(input);
    return 3;
  }
  float* output = malloc(output_bytes);
  if (!output) {
    free(expected);
    free(input);
    return 4;
  }
  jog_main(input, output);
  const size_t count = output_bytes / sizeof(float);
  float worst = 0.0f;
  size_t worst_at = 0;
  int status = 0;
  for (size_t i = 0; i < count; ++i) {
    const float error = fabsf(output[i] - expected[i]);
    const float limit = 1.0e-4f + 1.0e-4f * fabsf(expected[i]);
    if (!isfinite(output[i]) || error > limit) {
      fprintf(stderr,
              "output[%zu] = %.9g, expected %.9g, error %.9g, limit %.9g\n",
              i, output[i], expected[i], error, limit);
      status = 1;
      break;
    }
    if (error > worst) {
      worst = error;
      worst_at = i;
    }
  }
  if (status == 0)
    printf("%zu outputs agree; maximum absolute error %.9g at %zu\n", count,
           worst, worst_at);
  free(output);
  free(expected);
  free(input);
  return status;
}
