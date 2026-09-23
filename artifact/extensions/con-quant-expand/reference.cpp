#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() != "qadd") continue;
    for (auto key : {"lhs_scale", "rhs_scale", "output_scale"}) {
      if (op->getAttrOfType<mlir::FloatAttr>(key).getValueAsDouble() <= 0) {
        llvm::errs() << "invalid-scale\n";
        std::exit(5);
      }
    }
  }
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (op.getCallee() != "qadd") continue;
    mlir::OpBuilder builder(op);
    auto output = llvm::cast<mlir::RankedTensorType>(op.getResult(0).getType());
    auto floating = mlir::RankedTensorType::get(output.getShape(), builder.getF32Type());
    auto zeros = op->getAttrOfType<mlir::ArrayAttr>("zeros");
    auto call = [&](llvm::StringRef name, mlir::ValueRange inputs, mlir::Type type,
                    mlir::Attribute scale = {}, mlir::Attribute zero = {}) {
      auto node = builder.create<mlir::func::CallOp>(op.getLoc(), name, mlir::TypeRange{type}, inputs);
      if (scale) {
        node->setAttr("scale", scale);
        node->setAttr("zero", zero);
      }
      return node.getResult(0);
    };
    auto left = call("dequantize", {op.getOperand(0)}, floating, op->getAttr("lhs_scale"), zeros[0]);
    auto right = call("dequantize", {op.getOperand(1)}, floating, op->getAttr("rhs_scale"), zeros[1]);
    auto total = call("add", {left, right}, floating);
    auto result = call("quantize", {total}, output, op->getAttr("output_scale"), zeros[2]);
    op.getResult(0).replaceAllUsesWith(result);
    op.erase();
  }
}
