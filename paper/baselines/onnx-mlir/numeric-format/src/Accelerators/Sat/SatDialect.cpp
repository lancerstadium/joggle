/* SPDX-License-Identifier: Apache-2.0 */

#include "src/Accelerators/Sat/SatDialect.hpp"

#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/Hashing.h"

using namespace mlir;

namespace onnx_mlir::sat {
namespace detail {

struct IntTypeStorage final : TypeStorage {
  using KeyTy = unsigned;

  explicit IntTypeStorage(unsigned width) : width(width) {}
  bool operator==(const KeyTy &key) const { return width == key; }
  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_value(key);
  }
  static IntTypeStorage *construct(TypeStorageAllocator &allocator,
      const KeyTy &key) {
    return new (allocator.allocate<IntTypeStorage>()) IntTypeStorage(key);
  }

  unsigned width;
};

} // namespace detail

IntType IntType::get(MLIRContext *context, unsigned width) {
  return Base::get(context, width);
}

unsigned IntType::getWidth() const { return getImpl()->width; }

SatDialect::SatDialect(MLIRContext *context)
    : Dialect(getDialectNamespace(), context, TypeID::get<SatDialect>()) {
  addTypes<IntType>();
  allowUnknownOperations();
}

Type SatDialect::parseType(DialectAsmParser &parser) const {
  StringRef keyword;
  unsigned width = 0;
  if (parser.parseKeyword(&keyword) || keyword != "int" ||
      parser.parseLess() || parser.parseInteger(width) || parser.parseGreater())
    return {};
  if (width < 2 || width > 63) {
    parser.emitError(parser.getCurrentLocation(),
        "sat integer width must be between 2 and 63");
    return {};
  }
  return IntType::get(getContext(), width);
}

void SatDialect::printType(Type type, DialectAsmPrinter &printer) const {
  auto integer = dyn_cast<IntType>(type);
  if (!integer)
    llvm_unreachable("unexpected sat type");
  printer << "int<" << integer.getWidth() << ">";
}

} // namespace onnx_mlir::sat
