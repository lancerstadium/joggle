#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/OpDefinition.h"
#include "llvm/Support/raw_ostream.h"

namespace {
bool validLayout(mlir::StringAttr layout) {
  return layout && (layout.getValue() == "NCHW" || layout.getValue() == "NHWC");
}

mlir::RankedTensorType infer(mlir::RankedTensorType input,
                             mlir::StringAttr source, mlir::StringAttr destination) {
  llvm::SmallVector<int64_t> shape;
  for (char axis : destination.getValue())
    shape.push_back(input.getDimSize(source.getValue().find(axis)));
  return mlir::RankedTensorType::get(shape, input.getElementType());
}

class Relayout : public mlir::Op<Relayout, mlir::OpTrait::OneOperand,
                                mlir::OpTrait::OneResult, mlir::OpTrait::ZeroRegions> {
public:
  using Op::Op;
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Relayout)
  static llvm::StringRef getOperationName() { return "extension.relayout"; }
  static llvm::ArrayRef<llvm::StringRef> getAttributeNames() { return {}; }

  static void build(mlir::OpBuilder &, mlir::OperationState &state, mlir::Value input,
                    mlir::StringAttr source, mlir::StringAttr destination) {
    state.addOperands(input);
    state.addTypes(infer(llvm::cast<mlir::RankedTensorType>(input.getType()), source, destination));
    state.addAttribute("src", source);
    state.addAttribute("dst", destination);
  }

  mlir::LogicalResult verify() {
    auto input = llvm::dyn_cast<mlir::RankedTensorType>(getOperand().getType());
    if (!input || input.getRank() != 4) return emitOpError("rank-mismatch");
    auto source = (*this)->getAttrOfType<mlir::StringAttr>("src");
    auto destination = (*this)->getAttrOfType<mlir::StringAttr>("dst");
    if (!validLayout(source) || !validLayout(destination)) return emitOpError("invalid-layout");
    if ((*this)->getResult(0).getType() != infer(input, source, destination))
      return emitOpError("result-type-mismatch");
    return mlir::success();
  }
};

class Extension : public mlir::Dialect {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Extension)
  explicit Extension(mlir::MLIRContext *context)
      : Dialect("extension", context, mlir::TypeID::get<Extension>()) {
    addOperations<Relayout>();
  }
  static llvm::StringRef getDialectNamespace() { return "extension"; }
};
}

void registerExtension(mlir::MLIRContext &context) {
  context.getOrLoadDialect<Extension>();
}

mlir::Operation *constructExtension(mlir::OpBuilder &builder, mlir::ValueRange operands,
                                    llvm::ArrayRef<mlir::NamedAttribute> attributes) {
  if (operands.size() != 1) return nullptr;
  auto input = llvm::dyn_cast<mlir::RankedTensorType>(operands[0].getType());
  if (!input || input.getRank() != 4) {
    llvm::errs() << "rank-mismatch\n";
    return nullptr;
  }
  auto dictionary = builder.getDictionaryAttr(attributes);
  auto source = dictionary.getAs<mlir::StringAttr>("src");
  auto destination = dictionary.getAs<mlir::StringAttr>("dst");
  if (!validLayout(source) || !validLayout(destination)) {
    llvm::errs() << "invalid-layout\n";
    return nullptr;
  }
  return builder.create<Relayout>(builder.getUnknownLoc(), operands[0], source, destination);
}
