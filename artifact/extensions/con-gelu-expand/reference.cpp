#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <cstdlib>

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() != "gelu") continue;
    auto type = llvm::cast<mlir::RankedTensorType>(op.getOperand(0).getType());
    if (!type.getElementType().isF32() && !type.getElementType().isF64()) {
      llvm::errs() << "unsupported-element-type\n";
      std::exit(5);
    }
  }
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (op.getCallee() != "gelu") continue;
    mlir::OpBuilder builder(op);
    auto x = op.getOperand(0);
    auto call = [&](llvm::StringRef name, mlir::ValueRange inputs) {
      return builder.create<mlir::func::CallOp>(op.getLoc(), name, mlir::TypeRange{x.getType()}, inputs).getResult(0);
    };
    auto splat = [&](double value) {
      auto result = call("splat", {});
      result.getDefiningOp()->setAttr("value", builder.getF64FloatAttr(value));
      return result;
    };
    auto half = splat(0.5), one = splat(1.0), root = splat(std::sqrt(2.0));
    auto scaled = call("div", {x, root});
    auto error = call("erf", {scaled});
    auto total = call("add", {one, error});
    auto hx = call("mul", {half, x});
    auto result = call("mul", {hx, total});
    op.getResult(0).replaceAllUsesWith(result);
    op.erase();
  }
}
