#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/STLExtras.h"

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (op.getCallee() != "add" || op.getNumOperands() != 2 || op.getNumResults() != 1)
      continue;
    for (unsigned side = 0; side < 2; ++side) {
      auto zero = op.getOperand(side), other = op.getOperand(1 - side);
      auto constant = zero.getDefiningOp<mlir::func::CallOp>();
      if (!constant || constant.getCallee() != "splat" || other.getType() != op.getResult(0).getType())
        continue;
      auto zt = llvm::dyn_cast<mlir::RankedTensorType>(zero.getType());
      auto ot = llvm::dyn_cast<mlir::RankedTensorType>(other.getType());
      if (!zt || !ot || zt.getElementType() != ot.getElementType())
        continue;
      auto value = constant->getAttr("value");
      bool valid = false;
      if (auto integer = llvm::dyn_cast_if_present<mlir::IntegerAttr>(value))
        valid = integer.getValue().isZero();
      if (auto floating = llvm::dyn_cast_if_present<mlir::FloatAttr>(value)) {
        auto nsz = op->getAttrOfType<mlir::BoolAttr>("no_signed_zeros");
        valid = floating.getValue().isZero() && !floating.getValue().isNegative() && nsz && nsz.getValue();
      }
      if (!valid)
        continue;
      op.getResult(0).replaceAllUsesWith(other);
      op.erase();
      if (zero.use_empty())
        constant.erase();
      break;
    }
  }
}
