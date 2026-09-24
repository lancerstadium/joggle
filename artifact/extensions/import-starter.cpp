#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include <stdexcept>

mlir::OwningOpRef<mlir::ModuleOp> readExtension(llvm::StringRef source,
                                               mlir::MLIRContext &context) {
  throw std::runtime_error("not-implemented");
}
