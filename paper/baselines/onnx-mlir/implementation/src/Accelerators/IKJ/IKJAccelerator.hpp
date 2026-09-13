/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ONNX_MLIR_IKJ_ACCELERATOR_H
#define ONNX_MLIR_IKJ_ACCELERATOR_H

#include "src/Accelerators/Accelerator.hpp"

namespace onnx_mlir::accel {

class IKJAccelerator final : public Accelerator {
public:
  static IKJAccelerator *getInstance();

  static bool classof(const Accelerator *accelerator) {
    return accelerator->getKind() == Accelerator::Kind::IKJ;
  }
  static bool classof(const IKJAccelerator *) { return true; }

  uint64_t getVersionNumber() const final;

  void addPasses(mlir::OwningOpRef<mlir::ModuleOp> &module,
      mlir::PassManager &manager,
      onnx_mlir::EmissionTargetType &emissionTarget,
      std::string outputNameNoExt) const final;
  void registerDialects(mlir::DialectRegistry &registry) const final;
  void registerPasses(int optLevel) const final;
  void configurePasses() const final;

  mlir::MemRefType convertTensorTypeToMemRefType(
      mlir::TensorType tensorType) const final;
  void conversionTargetONNXToKrnl(
      mlir::ConversionTarget &target) const final;
  void rewritePatternONNXToKrnl(mlir::RewritePatternSet &patterns,
      mlir::TypeConverter &typeConverter, mlir::MLIRContext *context) const final;

  void conversionTargetKrnlToLLVM(
      mlir::ConversionTarget &target) const final;
  void rewritePatternKrnlToLLVM(mlir::RewritePatternSet &patterns,
      mlir::LLVMTypeConverter &typeConverter,
      mlir::MLIRContext *context) const final;

private:
  IKJAccelerator();
};

} // namespace onnx_mlir::accel

#endif
