#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>
#include <optional>
#include <vector>

[[noreturn]] static void invalid() {
  llvm::errs() << "invalid-permutation\n";
  std::exit(5);
}

static std::optional<std::vector<int64_t>> permutation(mlir::func::CallOp op) {
  if (!op || !op.getCallee().starts_with("transpose_")) return std::nullopt;
  if (op.getNumOperands() != 1 || op.getNumResults() != 1) invalid();
  auto a = llvm::dyn_cast<mlir::RankedTensorType>(op.getOperand(0).getType());
  auto b = llvm::dyn_cast<mlir::RankedTensorType>(op.getResult(0).getType());
  auto attr = op->getAttrOfType<mlir::ArrayAttr>("perm");
  if (!a || !b || !attr || a.getRank() != b.getRank() ||
      attr.size() != size_t(a.getRank()) || a.getElementType() != b.getElementType()) invalid();
  std::vector<int64_t> p;
  std::vector<bool> seen(a.getRank(), false);
  for (auto item : attr) {
    auto axis = llvm::dyn_cast<mlir::IntegerAttr>(item);
    if (!axis) invalid();
    auto i = axis.getInt();
    if (i < 0 || i >= a.getRank() || seen[i] || b.getDimSize(p.size()) != a.getDimSize(i)) invalid();
    seen[i] = true;
    p.push_back(i);
  }
  return p;
}

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) permutation(op);
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    auto q = permutation(op);
    if (!q) continue;
    auto input = op.getOperand(0);
    auto inner = input.getDefiningOp<mlir::func::CallOp>();
    auto p = permutation(inner);
    if (!p) continue;
    bool inverse = true;
    for (size_t i = 0; i < q->size(); ++i) inverse &= (*p)[(*q)[i]] == int64_t(i);
    if (!inverse) continue;
    op.getResult(0).replaceAllUsesWith(inner.getOperand(0));
    op.erase();
    if (input.use_empty()) inner.erase();
  }
}
