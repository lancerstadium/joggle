/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "src/Accelerators/Policy/PolicyAccelerator.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "src/Compiler/CompilerOptions.hpp"
#include "src/Compiler/CompilerPasses.hpp"

#include <limits>

using namespace mlir;

namespace onnx_mlir {
namespace {

llvm::cl::opt<int64_t> callWeight("policy-call-weight",
    llvm::cl::desc("Weight assigned to call operations"), llvm::cl::init(4));
llvm::cl::opt<int64_t> otherWeight("policy-other-weight",
    llvm::cl::desc("Weight assigned to non-call operations"), llvm::cl::init(1));
llvm::cl::opt<int64_t> maxExtent("policy-max-extent",
    llvm::cl::desc("Maximum static extent accepted by the fusion policy"),
    llvm::cl::init(std::numeric_limits<int64_t>::max()));
llvm::cl::opt<int64_t> maxCalls("policy-max-calls",
    llvm::cl::desc("Maximum recursive call count accepted by the fusion policy"),
    llvm::cl::init(std::numeric_limits<int64_t>::max()));
llvm::cl::opt<bool> reportPolicy("policy-report",
    llvm::cl::desc("Report weighted operation cost and fusion decisions"),
    llvm::cl::init(false));

struct PolicyPass final
    : PassWrapper<PolicyPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PolicyPass)

  StringRef getArgument() const final { return "policy-select"; }
  StringRef getDescription() const final {
    return "Apply the structural extension-study fusion policy";
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    int64_t calls = 0;
    int64_t others = 0;
    for (func::FuncOp function : module.getOps<func::FuncOp>()) {
      function.walk([&](Operation *operation) {
        if (operation == function.getOperation())
          return;
        if (isa<CallOpInterface>(operation))
          ++calls;
        else
          ++others;
      });
    }

    int64_t candidates = 0;
    int64_t accepted = 0;
    module.walk([&](Operation *producer) {
      if (producer->getNumResults() != 1 ||
          !producer->getResult(0).hasOneUse())
        return;
      auto producerType = dyn_cast<ShapedType>(producer->getResult(0).getType());
      if (!producerType || !producerType.hasStaticShape())
        return;
      Operation *consumer = *producer->getResult(0).getUsers().begin();
      if (consumer->getNumResults() != 1)
        return;
      auto consumerType = dyn_cast<ShapedType>(consumer->getResult(0).getType());
      if (!consumerType || !consumerType.hasStaticShape() ||
          producerType.getShape() != consumerType.getShape())
        return;

      ++candidates;
      int64_t recursiveCalls = 0;
      producer->walk([&](Operation *nested) {
        if (isa<CallOpInterface>(nested))
          ++recursiveCalls;
      });
      if (producerType.getNumElements() <= maxExtent &&
          recursiveCalls <= maxCalls)
        ++accepted;
    });

    disableKrnlOpFusion = candidates == 0 || accepted != candidates;
    if (reportPolicy) {
      llvm::outs() << "policy-cost=" << calls * callWeight + others * otherWeight
                   << " calls=" << calls << " others=" << others
                   << " candidates=" << candidates << " accepted=" << accepted
                   << " fusion="
                   << (disableKrnlOpFusion ? "disabled" : "enabled") << '\n';
    }
  }
};

} // namespace

namespace accel {

Accelerator *createPolicy() { return PolicyAccelerator::getInstance(); }

PolicyAccelerator *PolicyAccelerator::getInstance() {
  static PolicyAccelerator instance;
  return &instance;
}

PolicyAccelerator::PolicyAccelerator()
    : Accelerator(Accelerator::Kind::Policy) {
  acceleratorTargets.push_back(this);
  addCompilerConfig(CCM_SHARED_LIB_DEPS, {"RuntimePolicy"}, true);
}

uint64_t PolicyAccelerator::getVersionNumber() const { return 0x000100; }

void PolicyAccelerator::addPasses(mlir::OwningOpRef<mlir::ModuleOp> &module,
    mlir::PassManager &manager,
    onnx_mlir::EmissionTargetType &emissionTarget,
    std::string outputNameNoExt) const {
  manager.addPass(std::make_unique<PolicyPass>());
  onnx_mlir::addPasses(module, manager, emissionTarget, outputNameNoExt);
}

void PolicyAccelerator::registerDialects(mlir::DialectRegistry &) const {}

void PolicyAccelerator::registerPasses(int) const {
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<PolicyPass>();
  });
}

void PolicyAccelerator::configurePasses() const {}

mlir::MemRefType PolicyAccelerator::convertTensorTypeToMemRefType(
    mlir::TensorType) const {
  return nullptr;
}

void PolicyAccelerator::conversionTargetONNXToKrnl(
    mlir::ConversionTarget &) const {}

void PolicyAccelerator::rewritePatternONNXToKrnl(
    mlir::RewritePatternSet &, mlir::TypeConverter &,
    mlir::MLIRContext *) const {}

void PolicyAccelerator::conversionTargetKrnlToLLVM(
    mlir::ConversionTarget &) const {}

void PolicyAccelerator::rewritePatternKrnlToLLVM(
    mlir::RewritePatternSet &, mlir::LLVMTypeConverter &,
    mlir::MLIRContext *) const {}

} // namespace accel
} // namespace onnx_mlir
