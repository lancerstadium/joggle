#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"

static bool isCast(mlir::func::CallOp op) {
  if (!op || op.getNumOperands() != 1 || op.getNumResults() != 1)
    return false;
  auto a = llvm::dyn_cast<mlir::RankedTensorType>(op.getOperand(0).getType());
  auto b = llvm::dyn_cast<mlir::RankedTensorType>(op.getResult(0).getType());
  if (!a || !b)
    return false;
  std::string name;
  llvm::raw_string_ostream stream(name);
  stream << "cast_" << a.getElementType() << '_' << b.getElementType();
  return op.getCallee() == name;
}

static bool lossless(mlir::Type from, mlir::Type to) {
  auto a = llvm::cast<mlir::RankedTensorType>(from);
  auto b = llvm::cast<mlir::RankedTensorType>(to);
  if (a.getShape() != b.getShape())
    return false;
  auto ai = llvm::dyn_cast<mlir::IntegerType>(a.getElementType());
  auto bi = llvm::dyn_cast<mlir::IntegerType>(b.getElementType());
  if (ai && bi)
    return bi.getWidth() >= ai.getWidth();
  return a.getElementType().isF32() && b.getElementType().isF64();
}

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (!isCast(op))
      continue;
    auto input = op.getOperand(0), output = op.getResult(0);
    if (input.getType() == output.getType()) {
      output.replaceAllUsesWith(input);
      op.erase();
    } else if (auto inner = input.getDefiningOp<mlir::func::CallOp>(); isCast(inner)) {
      auto original = inner.getOperand(0);
      if (original.getType() == output.getType() && lossless(original.getType(), input.getType())) {
        output.replaceAllUsesWith(original);
        op.erase();
        if (input.use_empty())
          inner.erase();
      }
    }
  }
}
