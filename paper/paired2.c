#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void baseline_main(const float*, const unsigned char*, float*, float*);
void candidate_main(const float*, const unsigned char*, float*, float*);

static void* read_file(const char* path, size_t* bytes) {
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
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
    return -1.0;
  return (double)value.tv_sec + (double)value.tv_nsec * 1.0e-9;
}

static double run(void (*fn)(const float*, const unsigned char*, float*, float*),
                  const float* input, const unsigned char* data,
                  float* first, float* second) {
  const double begin = now();
  fn(input, data, first, second);
  const double end = now();
  return begin < 0.0 || end < begin ? -1.0 : end - begin;
}

static float error(const float* left, const float* right, size_t count) {
  float worst = 0.0f;
  for (size_t i = 0; i < count; ++i) {
    if (!isfinite(left[i]) || !isfinite(right[i]))
      return INFINITY;
    const float difference = fabsf(left[i] - right[i]);
    if (difference > worst)
      worst = difference;
  }
  return worst;
}

int main(int argc, char** argv) {
  if (argc != 6)
    return 2;
  size_t first_count = 0;
  size_t second_count = 0;
  size_t repetitions = 0;
  if (!number(argv[3], &first_count) || first_count == 0 ||
      !number(argv[4], &second_count) || second_count == 0 ||
      !number(argv[5], &repetitions) || repetitions == 0 ||
      first_count > SIZE_MAX / sizeof(float) ||
      second_count > SIZE_MAX / sizeof(float))
    return 3;

  size_t input_bytes = 0;
  size_t data_bytes = 0;
  float* input = read_file(argv[1], &input_bytes);
  unsigned char* data = read_file(argv[2], &data_bytes);
  float* baseline_first = calloc(first_count, sizeof(float));
  float* baseline_second = calloc(second_count, sizeof(float));
  float* candidate_first = calloc(first_count, sizeof(float));
  float* candidate_second = calloc(second_count, sizeof(float));
  if (!input || !data || !baseline_first || !baseline_second ||
      !candidate_first || !candidate_second ||
      input_bytes % sizeof(float) != 0 || data_bytes == 0)
    return 4;

  for (int i = 0; i < 3; ++i) {
    baseline_main(input, data, baseline_first, baseline_second);
    candidate_main(input, data, candidate_first, candidate_second);
  }

  puts("iteration,first,baseline_seconds,candidate_seconds,"
       "max_first_error,max_second_error");
  for (size_t i = 0; i < repetitions; ++i) {
    double baseline_time = 0.0;
    double candidate_time = 0.0;
    if (i % 2 == 0) {
      baseline_time = run(baseline_main, input, data,
                          baseline_first, baseline_second);
      candidate_time = run(candidate_main, input, data,
                           candidate_first, candidate_second);
    } else {
      candidate_time = run(candidate_main, input, data,
                           candidate_first, candidate_second);
      baseline_time = run(baseline_main, input, data,
                          baseline_first, baseline_second);
    }
    const float first_error = error(baseline_first, candidate_first,
                                    first_count);
    const float second_error = error(baseline_second, candidate_second,
                                     second_count);
    if (baseline_time < 0.0 || candidate_time < 0.0 ||
        !isfinite(first_error) || !isfinite(second_error))
      return 5;
    printf("%zu,%s,%.9f,%.9f,%.9g,%.9g\n", i,
           i % 2 == 0 ? "baseline" : "candidate", baseline_time,
           candidate_time, first_error, second_error);
  }

  free(candidate_second);
  free(candidate_first);
  free(baseline_second);
  free(baseline_first);
  free(data);
  free(input);
  return 0;
}
