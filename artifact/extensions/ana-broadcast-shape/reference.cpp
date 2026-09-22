#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstdint>
#include <vector>

llvm::json::Object analyze(mlir::ModuleOp module) {
  const auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
  const auto readShape = [&](llvm::StringRef key) {
    std::vector<int64_t> shape;
    for (mlir::Attribute extent : llvm::cast<mlir::ArrayAttr>(request.get(key)))
      shape.push_back(llvm::cast<mlir::IntegerAttr>(extent).getInt());
    return shape;
  };
  const auto lhs = readShape("lhs"), rhs = readShape("rhs");
  for (const auto *shape : {&lhs, &rhs})
    for (int64_t extent : *shape)
      if (extent < 0)
        return llvm::json::Object{{"error", "invalid-extent"}, {"phase", "build"}};
  const size_t rank = std::max(lhs.size(), rhs.size());
  std::vector<int64_t> result(rank);
  for (size_t offset = 0; offset < rank; ++offset) {
    const auto a = offset < lhs.size() ? lhs[lhs.size() - offset - 1] : 1;
    const auto b = offset < rhs.size() ? rhs[rhs.size() - offset - 1] : 1;
    if (a != b && a != 1 && b != 1)
      return llvm::json::Object{{"legal", false}, {"conflict_axis_from_end", int64_t(offset)}};
    result[rank - offset - 1] = a == 1 ? b : a;
  }
  llvm::json::Array shape;
  for (int64_t extent : result)
    shape.push_back(extent);
  return llvm::json::Object{{"legal", true}, {"shape", std::move(shape)}};
}
