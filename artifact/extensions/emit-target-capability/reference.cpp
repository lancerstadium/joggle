#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <string>
#include <vector>

llvm::json::Object analyze(mlir::ModuleOp module) {
  const auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
  auto ordered = [&](llvm::StringRef key) {
    std::vector<std::string> values;
    for (auto item : llvm::cast<mlir::ArrayAttr>(request.get(key)))
      values.push_back(llvm::cast<mlir::StringAttr>(item).getValue().str());
    std::sort(values.begin(), values.end());
    llvm::json::Array result;
    for (auto &value : values) result.push_back(value);
    return result;
  };
  return llvm::json::Object{
      {"target", llvm::cast<mlir::StringAttr>(request.get("target")).getValue().str()},
      {"pointer_bits", llvm::cast<mlir::IntegerAttr>(request.get("pointer_bits")).getInt()},
      {"little_endian", llvm::cast<mlir::IntegerAttr>(request.get("little_endian")).getInt() != 0},
      {"features", ordered("features")}, {"intrinsics", ordered("intrinsics")}};
}
