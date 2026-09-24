#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/JSON.h"
#include <cstdlib>
#include <string>

namespace {
[[noreturn]] void reject(llvm::StringRef message) {
  llvm::errs() << message << '\n';
  std::exit(5);
}
}

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() == "qint4_literal") {
      auto values = op->getAttrOfType<mlir::ArrayAttr>("values");
      if (!values) reject("literal-out-of-range");
      for (auto value : values) {
        auto integer = mlir::dyn_cast<mlir::IntegerAttr>(value);
        if (!integer || integer.getInt() < -8 || integer.getInt() > 7)
          reject("literal-out-of-range");
      }
    }
    if (op.getCallee() != "qint4_add") continue;
    if (op.getNumOperands() != 2 || op.getNumResults() != 1)
      reject("arity-mismatch");
    auto type = mlir::dyn_cast<mlir::RankedTensorType>(op.getResult(0).getType());
    if (!type || !type.getElementType().isSignlessInteger(4))
      reject("unsupported-element-type");
    for (auto value : op.getOperands())
      if (value.getType() != type) reject("shape-mismatch");
  }
  for (auto op : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (op.getCallee() != "qint4_add") continue;
    mlir::OpBuilder builder(op);
    auto type = mlir::cast<mlir::RankedTensorType>(op.getResult(0).getType());
    auto wide = mlir::RankedTensorType::get(type.getShape(), builder.getI8Type());
    auto call = [&](llvm::StringRef name, mlir::ValueRange operands, mlir::Type result) {
      return mlir::func::CallOp::create(builder, op.getLoc(), name,
                                      mlir::TypeRange{result}, operands);
    };
    auto left = call("sext_i4_i8", {op.getOperand(0)}, wide);
    auto right = call("sext_i4_i8", {op.getOperand(1)}, wide);
    auto sum = call("add_i8", {left.getResult(0), right.getResult(0)}, wide);
    auto clamped = call("clamp_i8", {sum.getResult(0)}, wide);
    clamped->setAttr("lower", builder.getI64IntegerAttr(-8));
    clamped->setAttr("upper", builder.getI64IntegerAttr(7));
    auto result = call("trunc_i8_i4", {clamped.getResult(0)}, type);
    op.getResult(0).replaceAllUsesWith(result.getResult(0));
    op.erase();
  }
}

llvm::json::Object analyze(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  mlir::func::CallOp clamp;
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() == "clamp_i8") {
      if (clamp) reject("expected-lowered-int4");
      clamp = op;
    }
  }
  if (!clamp) reject("expected-lowered-int4");
  const auto low = clamp->getAttrOfType<mlir::IntegerAttr>("lower").getInt();
  const auto high = clamp->getAttrOfType<mlir::IntegerAttr>("upper").getInt();
  std::string source = "#include <stdint.h>\n#include <stddef.h>\n";
  source += "#define LOW (" + std::to_string(low) + ")\n#define HIGH (" + std::to_string(high) + ")\n";
  source += R"(
int task_kernel(const int8_t *a, const int8_t *b, int8_t *values,
                uint8_t *packed, size_t n) {
  for (size_t i = 0; i < n; ++i)
    if (a[i] < -8 || a[i] > 7 || b[i] < -8 || b[i] > 7) return 1;
  for (size_t i = 0; i < n; ++i) {
    int sum = (int)a[i] + (int)b[i];
    if (sum < LOW) sum = LOW;
    if (sum > HIGH) sum = HIGH;
    values[i] = (int8_t)sum;
    uint8_t nibble = (uint8_t)((unsigned)sum & 15u);
    if ((i & 1u) == 0) packed[i / 2] = nibble;
    else packed[i / 2] |= (uint8_t)(nibble << 4);
  }
  return 0;
}
)";
  return llvm::json::Object{{"range", llvm::json::Array{low, high}},
                            {"symbol", "task_kernel"}, {"source", source}};
}
