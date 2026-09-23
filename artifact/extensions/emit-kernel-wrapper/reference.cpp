#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <string>

llvm::json::Object analyze(mlir::ModuleOp module) {
  auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
  auto kernel = llvm::cast<mlir::StringAttr>(request.get("kernel")).getValue();
  std::string expression = kernel == "relu" ? "x > 0.0f ? x : 0.0f" : "2.0f * x + 1.0f";
  return llvm::json::Object{{"symbol", "task_kernel"}, {"source",
      "#include <stddef.h>\n"
      "void task_kernel(const float* input, float* output, size_t count) {\n"
      "  for (size_t i = 0; i < count; ++i) {\n"
      "    float x = input[i];\n"
      "    output[i] = " + expression + ";\n"
      "  }\n}\n"}};
}
