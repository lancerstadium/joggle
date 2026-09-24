#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Builders.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/JSON.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

void transform(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  for (auto relu : llvm::make_early_inc_range(subject.getBody().front().getOps<mlir::func::CallOp>())) {
    if (relu.getCallee() != "relu") continue;
    auto quant = relu.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!quant || quant.getCallee() != "requantize") continue;
    auto bias = quant.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!bias || bias.getCallee() != "bias") continue;
    auto conv = bias.getOperand(0).getDefiningOp<mlir::func::CallOp>();
    if (!conv || conv.getCallee() != "qconv") continue;
    if (!conv.getResult(0).hasOneUse() || !bias.getResult(0).hasOneUse() ||
        !quant.getResult(0).hasOneUse()) continue;
    if (conv->getAttrOfType<mlir::StringAttr>("layout").getValue() != "NHWC" ||
        conv->getAttrOfType<mlir::StringAttr>("kernel_layout").getValue() != "HWIO" ||
        relu->getAttr("output_zero") != quant->getAttr("output_zero")) continue;
    mlir::OpBuilder builder(relu);
    llvm::SmallVector<mlir::Value> operands(conv.getOperands());
    operands.push_back(bias.getOperand(1));
    auto fused = builder.create<mlir::func::CallOp>(relu.getLoc(), "fused_qconv_relu", relu.getResultTypes(), operands);
    for (auto op : {conv, quant})
      for (auto attr : op->getAttrs())
        if (attr.getName() != "callee") fused->setAttr(attr.getName(), attr.getValue());
    relu.getResult(0).replaceAllUsesWith(fused.getResult(0));
    relu.erase(); quant.erase(); bias.erase(); conv.erase();
  }
}

llvm::json::Object analyze(mlir::ModuleOp module) {
  auto subject = module.lookupSymbol<mlir::func::FuncOp>("subject");
  mlir::func::CallOp fused;
  for (auto op : subject.getBody().front().getOps<mlir::func::CallOp>()) {
    if (op.getCallee() != "fused_qconv_relu") continue;
    if (fused) throw std::runtime_error("expected-fused-convolution");
    fused = op;
  }
  if (!fused) throw std::runtime_error("expected-fused-convolution");
  auto x = llvm::cast<mlir::RankedTensorType>(fused.getOperand(0).getType()).getShape();
  auto w = llvm::cast<mlir::RankedTensorType>(fused.getOperand(1).getType()).getShape();
  auto b = llvm::cast<mlir::RankedTensorType>(fused.getOperand(2).getType()).getShape();
  if (x[3] != w[2] || b.size() != 1 || b[0] != w[3]) throw std::runtime_error("shape-mismatch");
  std::ostringstream source;
  source << std::setprecision(17) << "#include <stdint.h>\n#include <math.h>\n"
         << "#define N (" << x[0] << ")\n#define H (" << x[1] << ")\n#define W (" << x[2]
         << ")\n#define CI (" << x[3] << ")\n#define KH (" << w[0] << ")\n#define KW (" << w[1]
         << ")\n#define CO (" << w[3] << ")\n#define ACC_SCALE ("
         << fused->getAttrOfType<mlir::FloatAttr>("acc_scale").getValueAsDouble()
         << ")\n#define OUT_SCALE (" << fused->getAttrOfType<mlir::FloatAttr>("out_scale").getValueAsDouble()
         << ")\n#define ZERO (" << fused->getAttrOfType<mlir::IntegerAttr>("output_zero").getInt() << ")\n";
  source << R"C(
int task_kernel(const int8_t *x, const int8_t *w, const int32_t *b, int8_t *y) {
  const int oh=H-KH+1, ow=W-KW+1;
  for (int n=0; n<N; ++n) for (int h=0; h<oh; ++h)
    for (int col=0; col<ow; ++col) for (int co=0; co<CO; ++co) {
      int64_t acc=b[co];
      for (int kh=0; kh<KH; ++kh) for (int kw=0; kw<KW; ++kw)
        for (int ci=0; ci<CI; ++ci)
          acc+=(int64_t)x[((n*H+h+kh)*W+col+kw)*CI+ci]*w[((kh*KW+kw)*CI+ci)*CO+co];
      if (acc<INT32_MIN || acc>INT32_MAX) return 1;
      double scaled=(double)acc*ACC_SCALE/OUT_SCALE;
      if (isnan(scaled)) return 2;
      double value=scaled<=0 ? ZERO : scaled>=127-ZERO ? 127 : nearbyint(scaled)+ZERO;
      y[((n*oh+h)*ow+col)*CO+co]=(int8_t)value;
    }
  return 0;
}
)C";
  return llvm::json::Object{{"symbol", "task_kernel"}, {"source", source.str()}};
}
