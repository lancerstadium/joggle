/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/Accelerators/IKJ/IKJAccelerator.hpp"

#include "src/Compiler/CompilerOptions.hpp"
#include "src/Compiler/CompilerPasses.hpp"
#include "src/Dialect/Krnl/DialectBuilder.hpp"
#include "src/Dialect/Mlir/DialectBuilder.hpp"
#include "src/Dialect/ONNX/ONNXOps.hpp"

#include "llvm/ADT/SmallVector.h"

using namespace mlir;

namespace onnx_mlir {
namespace {

/// A deliberately simple, shape-generic rank-two matrix implementation.  The
/// loop order is i-k-j: initialize C once, then update one row across the
/// reduction dimension.  This is an extension-surface experiment, not a
/// performance replacement for ONNX-MLIR's native matrix lowering.
struct IKJMatMul final : OpConversionPattern<ONNXMatMulOp> {
  IKJMatMul(TypeConverter &converter, MLIRContext *context)
      : OpConversionPattern(converter, context, PatternBenefit(10)) {}

  LogicalResult matchAndRewrite(ONNXMatMulOp op, ONNXMatMulOpAdaptor operands,
      ConversionPatternRewriter &rewriter) const final {
    auto leftType = dyn_cast<MemRefType>(operands.getA().getType());
    auto rightType = dyn_cast<MemRefType>(operands.getB().getType());
    Type converted =
        typeConverter->convertType(op.getOperation()->getResult(0).getType());
    auto resultType = dyn_cast_or_null<MemRefType>(converted);

    if (!leftType || !rightType || !resultType || leftType.getRank() != 2 ||
        rightType.getRank() != 2 || resultType.getRank() != 2 ||
        !leftType.getElementType().isF32() ||
        leftType.getElementType() != rightType.getElementType() ||
        leftType.getElementType() != resultType.getElementType())
      return rewriter.notifyMatchFailure(
          op, "IKJ requires rank-two float32 matrix operands and result");

    Location location = op.getLoc();
    MultiDialectBuilder<KrnlBuilder, MemRefBuilder, MathBuilder> create(
        rewriter, location);
    Value left = operands.getA();
    Value right = operands.getB();
    Value rows = create.mem.dim(left, 0);
    Value reduction = create.mem.dim(left, 1);
    Value columns = create.mem.dim(right, 1);
    SmallVector<Value, 2> dynamicDimensions;
    if (resultType.isDynamicDim(0))
      dynamicDimensions.push_back(rows);
    if (resultType.isDynamicDim(1))
      dynamicDimensions.push_back(columns);
    Value result = create.mem.alignedAlloc(resultType, dynamicDimensions);
    Value zero = create.math.constant(resultType.getElementType(), 0);
    create.krnl.memset(result, zero);

    ValueRange loops = create.krnl.defineLoops(3);
    Value indexZero = create.math.constantIndex(0);
    create.krnl.iterate(loops, loops, {indexZero, indexZero, indexZero},
        {rows, reduction, columns},
        [&](const KrnlBuilder &body, ValueRange indices) {
          MultiDialectBuilder<KrnlBuilder, MathBuilder> inside(body);
          Value a = inside.krnl.load(left, {indices[0], indices[1]});
          Value b = inside.krnl.load(right, {indices[1], indices[2]});
          Value c = inside.krnl.load(result, {indices[0], indices[2]});
          Value product = inside.math.mul(a, b);
          inside.krnl.store(
              inside.math.add(c, product), result, {indices[0], indices[2]});
        });

    rewriter.replaceOp(op, result);
    return success();
  }
};

} // namespace

namespace accel {

Accelerator *createIKJ() { return IKJAccelerator::getInstance(); }

IKJAccelerator *IKJAccelerator::getInstance() {
  static IKJAccelerator instance;
  return &instance;
}

IKJAccelerator::IKJAccelerator() : Accelerator(Accelerator::Kind::IKJ) {
  acceleratorTargets.push_back(this);
  addCompilerConfig(CCM_SHARED_LIB_DEPS, {"RuntimeIKJ"}, true);
}

uint64_t IKJAccelerator::getVersionNumber() const { return 0x000100; }

void IKJAccelerator::addPasses(mlir::OwningOpRef<mlir::ModuleOp> &module,
    mlir::PassManager &manager,
    onnx_mlir::EmissionTargetType &emissionTarget,
    std::string outputNameNoExt) const {
  onnx_mlir::addPasses(module, manager, emissionTarget, outputNameNoExt);
}

void IKJAccelerator::registerDialects(mlir::DialectRegistry &) const {}

void IKJAccelerator::registerPasses(int) const {}

void IKJAccelerator::configurePasses() const {}

mlir::MemRefType IKJAccelerator::convertTensorTypeToMemRefType(
    mlir::TensorType) const {
  return nullptr;
}

void IKJAccelerator::conversionTargetONNXToKrnl(
    mlir::ConversionTarget &) const {}

void IKJAccelerator::rewritePatternONNXToKrnl(
    mlir::RewritePatternSet &patterns, mlir::TypeConverter &typeConverter,
    mlir::MLIRContext *context) const {
  patterns.insert<IKJMatMul>(typeConverter, context);
}

void IKJAccelerator::conversionTargetKrnlToLLVM(
    mlir::ConversionTarget &) const {}

void IKJAccelerator::rewritePatternKrnlToLLVM(
    mlir::RewritePatternSet &, mlir::LLVMTypeConverter &,
    mlir::MLIRContext *) const {}

} // namespace accel
} // namespace onnx_mlir
