#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstdint>
#include <vector>

llvm::json::Object analyze(mlir::ModuleOp module) {
  using namespace mlir;
  const auto request = module->getAttrOfType<DictionaryAttr>("study.request");
  struct Allocation { int64_t begin, end, offset, bytes; };
  std::vector<Allocation> placed;
  llvm::json::Array allocations;
  int64_t peak = 0;
  for (auto attr : llvm::cast<ArrayAttr>(request.get("values"))) {
    auto value = llvm::cast<DictionaryAttr>(attr);
    auto size = llvm::dyn_cast<IntegerAttr>(value.get("bytes"));
    if (!size)
      return llvm::json::Object{{"error", "dynamic-storage-size"}, {"phase", "build"}};
    auto live = llvm::cast<ArrayAttr>(value.get("live"));
    int64_t begin = llvm::cast<IntegerAttr>(live[0]).getInt();
    int64_t end = llvm::cast<IntegerAttr>(live[1]).getInt();
    int64_t bytes = size.getInt(), offset = 0;
    for (;;) {
      int64_t next = offset;
      for (const auto &prior : placed) {
        const auto memoryEnd = prior.offset + prior.bytes;
        if (bytes > 0 && prior.bytes > 0 && begin <= prior.end && prior.begin <= end &&
            offset < memoryEnd && prior.offset < offset + bytes)
          next = std::max(next, ((memoryEnd + 15) / 16) * 16);
      }
      if (next == offset) break;
      offset = next;
    }
    placed.push_back({begin, end, offset, bytes});
    allocations.push_back(llvm::json::Object{
        {"id", llvm::cast<StringAttr>(value.get("id")).getValue().str()}, {"offset", offset}});
    peak = std::max(peak, offset + bytes);
  }
  return llvm::json::Object{{"allocations", std::move(allocations)}, {"peak_bytes", peak}};
}
