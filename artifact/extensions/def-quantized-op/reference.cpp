#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include <cmath>

namespace {
class QAdd : public mlir::Op<QAdd, mlir::OpTrait::NOperands<2>::Impl,
                             mlir::OpTrait::OneResult, mlir::OpTrait::ZeroRegions> {
public:
  using Op::Op;
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(QAdd)
  static llvm::StringRef getOperationName() { return "extension.qadd"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &, mlir::OperationState &state,
                    mlir::Value lhs, mlir::Value rhs,
                    llvm::ArrayRef<mlir::NamedAttribute> attributes) {
    state.addOperands({lhs, rhs});
    state.addTypes(lhs.getType());
    state.addAttributes(attributes);
  }

  mlir::LogicalResult verify() {
    auto lhs = llvm::dyn_cast<mlir::RankedTensorType>(getOperand(0).getType());
    auto rhs = llvm::dyn_cast<mlir::RankedTensorType>(getOperand(1).getType());
    auto result = llvm::dyn_cast<mlir::RankedTensorType>((*this)->getResult(0).getType());
    for (auto type : {lhs, rhs, result})
      if (!type || !type.getElementType().isSignlessInteger(8))
        return emitOpError("operand-type-mismatch");
    if (lhs.getShape() != rhs.getShape() || lhs.getShape() != result.getShape())
      return emitOpError("shape-mismatch");
    for (llvm::StringRef name : {"lhs_scale", "rhs_scale", "output_scale"}) {
      auto scale = (*this)->getAttrOfType<mlir::FloatAttr>(name);
      if (!scale || !scale.getType().isF64() ||
          !std::isfinite(scale.getValueAsDouble()) || scale.getValueAsDouble() <= 0)
        return emitOpError("invalid-scale");
    }
    for (llvm::StringRef name : {"lhs_zero", "rhs_zero", "output_zero"}) {
      auto zero = (*this)->getAttrOfType<mlir::IntegerAttr>(name);
      if (!zero || !zero.getType().isSignlessInteger(32))
        return emitOpError("invalid-zero-point");
    }
    return mlir::success();
  }
};

class Extension : public mlir::Dialect {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Extension)
  explicit Extension(mlir::MLIRContext *context)
      : Dialect("extension", context, mlir::TypeID::get<Extension>()) {
    addOperations<QAdd>();
  }
  static llvm::StringRef getDialectNamespace() { return "extension"; }
};
}

void registerExtension(mlir::MLIRContext &context) {
  context.getOrLoadDialect<Extension>();
}

mlir::Operation *constructExtension(mlir::OpBuilder &builder, mlir::ValueRange operands,
                                    llvm::ArrayRef<mlir::NamedAttribute> attributes) {
  if (operands.size() != 2) return nullptr;
  return builder.create<QAdd>(builder.getUnknownLoc(), operands[0], operands[1], attributes);
}
