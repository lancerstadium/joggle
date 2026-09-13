#ifndef JOGGLE_STUDY_TVM_EXTERNAL_BRIDGE_H
#define JOGGLE_STUDY_TVM_EXTERNAL_BRIDGE_H

void model_main(const float* a, const float* b, float* out);
void model_wide(const float* a, const float* b, float* out);
void model_conv(const float* x, const float* weight, float* out);
void model_fixed_conv(const float* x, float* out);
void model_biased_conv(const float* x, const float* weight, const float* bias,
                       float* out);
void model_extrema(const float* x, float* low, float* high);

#endif
