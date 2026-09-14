#include <stdint.h>

void nms_c_main(const float* boxes, const float* scores,
                const int64_t* maximum, const float* overlap,
                const float* threshold, int64_t* result,
                int64_t* result_dim_0);

int main(void) {
  const float boxes[16] = {
      0.0f, 0.0f, 1.0f, 1.0f,
      0.0f, 0.0f, 0.9f, 0.9f,
      10.0f, 10.0f, 11.0f, 11.0f,
      20.0f, 20.0f, 21.0f, 21.0f,
  };
  const float scores[8] = {
      0.9f, 0.8f, 0.7f, 0.1f,
      0.2f, 0.3f, 0.4f, 0.5f,
  };
  const int64_t maximum = 2;
  const float overlap = 0.5f;
  const float threshold = 0.0f;
  const int64_t expected[12] = {
      0, 0, 0,
      0, 0, 2,
      0, 1, 3,
      0, 1, 2,
  };
  int64_t result[24] = {0};
  int64_t rows = -1;
  nms_c_main(boxes, scores, &maximum, &overlap, &threshold, result, &rows);
  if (rows != 4)
    return 1;
  for (int64_t i = 0; i < rows * 3; ++i)
    if (result[i] != expected[i])
      return 2;
  return 0;
}
