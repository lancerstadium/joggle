#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/JSON.h"

llvm::json::Object analyze(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  llvm::DenseMap<mlir::Operation *, int64_t> positions;
  int64_t index = 0;
  for (auto call : subject.getBody().front().getOps<mlir::func::CallOp>())
    positions[call] = index++;
  const int axis = subject->getAttrOfType<mlir::StringAttr>("layout").getValue() == "NCHW" ? 1 : 3;
  llvm::json::Array matches;
  for (auto relu : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (relu.getCallee() != "relu" || relu.getNumOperands() != 1)
      continue;
    auto bias = relu.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!bias || bias.getCallee() != "bias_add" || bias.getNumOperands() != 2 ||
        bias.getNumResults() != 1 || !bias.getResult(0).hasOneUse())
      continue;
    auto conv = bias.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!conv || conv.getCallee() != "conv2d" || conv.getNumResults() != 1 ||
        !conv.getResult(0).hasOneUse())
      continue;
    auto outputType = llvm::dyn_cast<mlir::RankedTensorType>(conv.getResult(0).getType());
    auto biasType = llvm::dyn_cast<mlir::RankedTensorType>(bias.getOperand(1).getType());
    if (outputType && biasType && outputType.getRank() == 4 && biasType.getRank() == 1 &&
        outputType.getDimSize(axis) == biasType.getDimSize(0))
      matches.push_back(llvm::json::Array{positions[conv], positions[bias], positions[relu]});
  }
  return llvm::json::Object{{"matches", std::move(matches)}};
}
