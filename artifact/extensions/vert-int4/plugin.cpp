// Native pass-plugin publication for the lowbit feature package.
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/Plugins/PassPlugin.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <exception>

void transform(mlir::ModuleOp);
llvm::json::Object analyze(mlir::ModuleOp);

namespace {
struct Transform : mlir::PassWrapper<Transform, mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Transform)
  llvm::StringRef getArgument() const final { return "study-lowbit-lower"; }
  llvm::StringRef getDescription() const final { return "Apply the lowbit graph transformation"; }
  void getDependentDialects(mlir::DialectRegistry &registry) const override {
    registry.insert<mlir::func::FuncDialect>();
  }
  void runOnOperation() override {
    try {
      transform(getOperation());
    } catch (const std::exception &error) {
      getOperation().emitError(error.what());
      signalPassFailure();
    }
  }
};

struct Emit : mlir::PassWrapper<Emit, mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(Emit)
  Emit() = default;
  Emit(const Emit &other) : PassWrapper(other) {}
  llvm::StringRef getArgument() const final { return "study-lowbit-c"; }
  llvm::StringRef getDescription() const final { return "Emit the lowbit C kernel"; }
  Option<std::string> output{*this, "output", llvm::cl::desc("C artifact path"),
                             llvm::cl::init("")};
  void runOnOperation() override {
    if (output.empty()) {
      getOperation().emitError("an output path is required");
      return signalPassFailure();
    }
    try {
      auto result = analyze(getOperation());
      auto source = result.getString("source");
      if (!source) {
        getOperation().emitError("the feature did not produce C source");
        return signalPassFailure();
      }
      std::error_code error;
      llvm::raw_fd_ostream stream(output, error);
      if (error) {
        getOperation().emitError(error.message());
        return signalPassFailure();
      }
      stream << *source;
      stream.close();
      if (stream.has_error()) {
        getOperation().emitError("could not write C artifact");
        signalPassFailure();
        stream.clear_error();
      }
    } catch (const std::exception &error) {
      getOperation().emitError(error.what());
      signalPassFailure();
    }
  }
};
} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK mlir::PassPluginLibraryInfo mlirGetPassPluginInfo() {
  return {MLIR_PLUGIN_API_VERSION, "StudyLowbit", LLVM_VERSION_STRING, []() {
    mlir::PassRegistration<Transform>();
    mlir::PassRegistration<Emit>();
  }};
}
