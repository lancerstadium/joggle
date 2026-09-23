#include "mlir/IR/Dialect.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/IR/Types.h"
#include "llvm/ADT/Hashing.h"

namespace {
struct FixedStorage : mlir::TypeStorage {
  using KeyTy = std::pair<int64_t, int64_t>;
  int64_t width, frac;
  FixedStorage(int64_t w, int64_t f) : width(w), frac(f) {}
  bool operator==(const KeyTy &key) const { return key == KeyTy(width, frac); }
  static llvm::hash_code hashKey(const KeyTy &key) { return llvm::hash_combine(key.first, key.second); }
  static FixedStorage *construct(mlir::TypeStorageAllocator &allocator, const KeyTy &key) {
    return new (allocator.allocate<FixedStorage>()) FixedStorage(key.first, key.second);
  }
};
class FixedPoint : public mlir::Type::TypeBase<FixedPoint, mlir::Type, FixedStorage> {
public:
  using Base::Base;
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FixedPoint)
  static constexpr llvm::StringLiteral name = "extension.fx";
  int64_t width() const { return getImpl()->width; }
  int64_t frac() const { return getImpl()->frac; }
  static mlir::LogicalResult verifyInvariants(llvm::function_ref<mlir::InFlightDiagnostic()> error,
                                    int64_t width, int64_t frac) {
    if (width < 2 || width > 32 || frac < 0 || frac >= width)
      return error() << "invalid-type-parameter";
    return mlir::success();
  }
};
class Extension : public mlir::Dialect {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Extension)
  explicit Extension(mlir::MLIRContext *context)
      : Dialect("extension", context, mlir::TypeID::get<Extension>()) { addTypes<FixedPoint>(); }
  static llvm::StringRef getDialectNamespace() { return "extension"; }
  mlir::Type parseType(mlir::DialectAsmParser &parser) const override {
    llvm::StringRef name;
    int64_t width, frac;
    if (parser.parseKeyword(&name) || name != "fx" || parser.parseLess() ||
        parser.parseInteger(width) || parser.parseComma() || parser.parseInteger(frac) ||
        parser.parseGreater()) return {};
    return FixedPoint::getChecked([&]() { return parser.emitError(parser.getCurrentLocation()); },
                                  getContext(), width, frac);
  }
  void printType(mlir::Type type, mlir::DialectAsmPrinter &printer) const override {
    auto fixed = llvm::cast<FixedPoint>(type);
    printer << "fx<" << fixed.width() << "," << fixed.frac() << ">";
  }
};
}

void registerExtension(mlir::MLIRContext &context) { context.getOrLoadDialect<Extension>(); }
