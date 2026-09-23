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
    if (op.getCallee() != "conv2d_nhwc") continue;
    auto x = llvm::cast<mlir::RankedTensorType>(op.getOperand(0).getType());
    auto w = llvm::cast<mlir::RankedTensorType>(op.getOperand(1).getType());
    if (x.getDimSize(3) != w.getDimSize(2)) {
      llvm::errs() << "channel-mismatch\n";
      std::exit(5);
    }
  }
  auto permuted = [](mlir::Type type, llvm::ArrayRef<int64_t> p) {
    auto tensor = llvm::cast<mlir::RankedTensorType>(type);
    llvm::SmallVector<int64_t> shape;
    for (auto i : p) shape.push_back(tensor.getDimSize(i));
    return mlir::RankedTensorType::get(shape, tensor.getElementType());
  };
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (op.getCallee() != "conv2d_nhwc") continue;
    mlir::OpBuilder builder(op);
    auto transpose = [&](llvm::StringRef name, mlir::Value value, llvm::ArrayRef<int64_t> p) {
      auto node = builder.create<mlir::func::CallOp>(op.getLoc(), name,
          mlir::TypeRange{permuted(value.getType(), p)}, mlir::ValueRange{value});
      node->setAttr("perm", builder.getI64ArrayAttr(p));
      return node.getResult(0);
    };
    auto x = transpose("transpose_input", op.getOperand(0), {0, 3, 1, 2});
    auto w = transpose("transpose_weight", op.getOperand(1), {3, 2, 0, 1});
    auto conv = builder.create<mlir::func::CallOp>(op.getLoc(), "conv2d_nchw",
        mlir::TypeRange{permuted(op.getResult(0).getType(), {0, 3, 1, 2})}, mlir::ValueRange{x, w});
    for (auto key : {"stride", "pad", "dilation"}) conv->setAttr(key, op->getAttr(key));
    auto result = transpose("transpose_output", conv.getResult(0), {0, 2, 3, 1});
    op.getResult(0).replaceAllUsesWith(result);
    op.erase();
  }
}
