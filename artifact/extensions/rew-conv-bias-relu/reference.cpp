#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto relu : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (relu.getCallee() != "relu") continue;
    auto bias = relu.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!bias || bias.getCallee() != "bias_add") continue;
    auto conv = bias.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!conv || conv.getCallee() != "conv2d" || !conv.getResult(0).hasOneUse() ||
        !bias.getResult(0).hasOneUse()) continue;
    auto shape = llvm::dyn_cast<mlir::RankedTensorType>(conv.getResult(0).getType());
    auto bshape = llvm::dyn_cast<mlir::RankedTensorType>(bias.getOperand(1).getType());
    const int axis = conv->getAttrOfType<mlir::StringAttr>("layout").getValue() == "NCHW" ? 1 : 3;
    auto declaredAxis = bias->getAttrOfType<mlir::IntegerAttr>("axis");
    if (!shape || !bshape || shape.getRank() != 4 || bshape.getRank() != 1 ||
        shape.getDimSize(axis) != bshape.getDimSize(0) || !declaredAxis || declaredAxis.getInt() != axis)
      continue;
    mlir::OpBuilder builder(relu);
    llvm::SmallVector<mlir::Value> operands(conv.getOperands());
    operands.push_back(bias.getOperand(1));
    auto fused = builder.create<mlir::func::CallOp>(relu.getLoc(), "fused_conv_bias_relu",
        relu.getResultTypes(), operands);
    for (auto attr : conv->getAttrs())
      if (attr.getName() != "callee") fused->setAttr(attr.getName(), attr.getValue());
    relu.getResult(0).replaceAllUsesWith(fused.getResult(0));
    relu.erase();
    bias.erase();
    conv.erase();
  }
}
