#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int read_exact(const char* path, void* data, size_t bytes) {
  FILE* file = fopen(path, "rb");
  if (!file)
    return 0;
  const int ok = fread(data, 1, bytes, file) == bytes && fgetc(file) == EOF;
  return fclose(file) == 0 && ok;
}

int main(int argc, char** argv) {
  enum { input_count = 1 * 3 * 224 * 224, output_count = 1000 };
  if (argc != 3)
    return 2;
  float* input = malloc(sizeof(float) * input_count);
  float* output = malloc(sizeof(float) * output_count);
  float* expected = malloc(sizeof(float) * output_count);
  if (!input || !output || !expected)
    return 3;
  if (!read_exact(argv[1], input, sizeof(float) * input_count) ||
      !read_exact(argv[2], expected, sizeof(float) * output_count))
    return 4;
  jog_main(input, output);
  float worst = 0.0f;
  size_t worst_at = 0;
  for (size_t i = 0; i < output_count; ++i) {
    const float error = fabsf(output[i] - expected[i]);
    const float limit = 1.0e-4f + 1.0e-4f * fabsf(expected[i]);
    if (!isfinite(output[i]) || error > limit) {
      fprintf(stderr,
              "output[%zu] = %.9g, expected %.9g, error %.9g, limit %.9g\n",
              i, output[i], expected[i], error, limit);
      return 1;
    }
    if (error > worst) {
      worst = error;
      worst_at = i;
    }
  }
  printf("1000 outputs agree; maximum absolute error %.9g at %zu\n", worst,
         worst_at);
  free(expected);
  free(output);
  free(input);
  return 0;
}
