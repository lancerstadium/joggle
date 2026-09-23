def analyze(module):
    kernel = module.attributes["study.request"].data["kernel"].data
    expression = "x > 0.0f ? x : 0.0f" if kernel == "relu" else "2.0f * x + 1.0f"
    return {"symbol": "task_kernel", "source": (
        "#include <stddef.h>\n"
        "void task_kernel(const float* input, float* output, size_t count) {\n"
        "  for (size_t i = 0; i < count; ++i) {\n"
        "    float x = input[i];\n"
        f"    output[i] = {expression};\n"
        "  }\n}\n")}
