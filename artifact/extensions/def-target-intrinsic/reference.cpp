#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"

namespace {
class Dot4I8 : public mlir::Op<Dot4I8, mlir::OpTrait::NOperands<3>::Impl,
                             mlir::OpTrait::OneResult, mlir::OpTrait::ZeroRegions> {
public:
  using Op::Op;
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Dot4I8)
  static llvm::StringRef getOperationName() { return "extension.dot4_i8"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &builder, mlir::OperationState &state,
                    mlir::ValueRange operands,
                    llvm::ArrayRef<mlir::NamedAttribute> attributes) {
    state.addOperands(operands);
    state.addTypes(builder.getI32Type());
    state.addAttributes(attributes);
  }

  mlir::LogicalResult verify() {
    for (unsigned index : {0u, 1u}) {
      auto vector = llvm::dyn_cast<mlir::VectorType>(getOperand(index).getType());
      if (!vector || vector.getRank() != 1 || vector.getDimSize(0) != 4 ||
          vector.isScalable() || !vector.getElementType().isSignlessInteger(8))
        return emitOpError("operand-type-mismatch");
    }
    if (!getOperand(2).getType().isSignlessInteger(32))
      return emitOpError("operand-type-mismatch");
    if (!(*this)->getResult(0).getType().isSignlessInteger(32))
      return emitOpError("result-type-mismatch");
    return mlir::success();
  }
};

class Extension : public mlir::Dialect {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Extension)
  explicit Extension(mlir::MLIRContext *context)
      : Dialect("extension", context, mlir::TypeID::get<Extension>()) {
    addOperations<Dot4I8>();
  }
  static llvm::StringRef getDialectNamespace() { return "extension"; }
};
}

void registerExtension(mlir::MLIRContext &context) {
  context.getOrLoadDialect<Extension>();
}

mlir::Operation *constructExtension(mlir::OpBuilder &builder, mlir::ValueRange operands,
                                    llvm::ArrayRef<mlir::NamedAttribute> attributes) {
  if (operands.size() != 3) return nullptr;
  mlir::OperationState state(builder.getUnknownLoc(), Dot4I8::getOperationName());
  Dot4I8::build(builder, state, operands, attributes);
  return builder.create(state);
}
