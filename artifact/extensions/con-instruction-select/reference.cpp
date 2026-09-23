#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Builders.h"

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  mlir::Builder builder(module.getContext());
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() != "matmul" || op.getNumOperands() != 2 || op.getNumResults() != 1) continue;
    auto a = llvm::dyn_cast<mlir::RankedTensorType>(op.getOperand(0).getType());
    auto b = llvm::dyn_cast<mlir::RankedTensorType>(op.getOperand(1).getType());
    auto c = llvm::dyn_cast<mlir::RankedTensorType>(op.getResult(0).getType());
    if (!a || !b || !c || a.getRank() != 2 || b.getRank() != 2 || c.getRank() != 2 ||
        !a.getElementType().isF16() || !b.getElementType().isF16() || !c.getElementType().isF32()) continue;
    auto m = a.getDimSize(0), k = a.getDimSize(1), n = b.getDimSize(1);
    if (m <= 0 || k <= 0 || n <= 0 || m % 16 || k % 16 || n % 16 ||
        k != b.getDimSize(0) || c.getDimSize(0) != m || c.getDimSize(1) != n) continue;
    op.setCallee("mma_m16n16k16");
    op->setAttr("tiles", builder.getI64ArrayAttr({m / 16, n / 16, k / 16}));
  }
}
