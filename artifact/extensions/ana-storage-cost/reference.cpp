#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <cstdint>

llvm::json::Object analyze(mlir::ModuleOp module) {
  const auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
  int64_t elements = 1;
  for (mlir::Attribute value : llvm::cast<mlir::ArrayAttr>(request.get("shape"))) {
    const auto extent = llvm::cast<mlir::IntegerAttr>(value).getInt();
    if (extent < 0)
      return llvm::json::Object{{"error", "invalid-extent"}, {"phase", "build"}};
    elements *= extent;
  }
  const auto bits = elements * llvm::cast<mlir::IntegerAttr>(request.get("element_bits")).getInt();
  return llvm::json::Object{{"bytes", bits / 8 + (bits % 8 != 0)}};
}
