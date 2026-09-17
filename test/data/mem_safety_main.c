#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

float mem_safety_view_reuse(float a, float b);
float mem_safety_view_chain(float a, float b);
void mem_safety_return_view(float a, float b, float* out);
float mem_safety_view_shape(float a, int64_t count, int64_t replacement);
float mem_safety_make_shape(float a, bool choose, int64_t replacement);
float mem_safety_loop_reuse(float a, float b, int64_t count);
float mem_safety_nested_loop(float a, float b, int64_t count);
float mem_safety_branch_view(float a, float b, bool choose);
float mem_safety_selection(float a, float b, float c);

static int check(const char* name, float actual, float expected) {
  if (actual == expected)
    return 0;
  fprintf(stderr, "%s: expected %g, got %g\n", name, expected, actual);
  return 1;
}

int main(void) {
  int failed = 0;
  const float inputs[][2] = {{2, 7}, {-3, 5}, {0, -4}};
  for (int i = 0; i < 3; ++i) {
    const float a = inputs[i][0];
    const float b = inputs[i][1];
    failed |= check("selection", mem_safety_selection(a, b, 11), a + b + 11);
    failed |= check("view", mem_safety_view_reuse(a, b), a + b);
    failed |= check("view chain", mem_safety_view_chain(a, b), a + b);
    float out[4] = {0};
    mem_safety_return_view(a, b, out);
    for (int j = 0; j < 4; ++j)
      failed |= check("returned view", out[j], j == 0 ? a + b : a);
    failed |= check("branch false", mem_safety_branch_view(a, b, false), a);
    failed |= check("branch true", mem_safety_branch_view(a, b, true), a + b);
    failed |= check("view shape", mem_safety_view_shape(a, 2, 3), a + 5);
    failed |= check("make shape false", mem_safety_make_shape(a, false, 3), a + 5);
    failed |= check("make shape true", mem_safety_make_shape(a, true, 3), a + 6);
    for (int64_t n = 0; n <= 3; ++n) {
      failed |= check("loop", mem_safety_loop_reuse(a, b, n), (float)n * (a + b));
      failed |= check("nested loop", mem_safety_nested_loop(a, b, n),
                      (float)(2 * n) * (a + b));
    }
  }
  return failed;
}
