#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Builders.h"

void registerExtension(mlir::MLIRContext &context) {}

mlir::Operation *constructExtension(mlir::OpBuilder &, mlir::ValueRange,
                                    llvm::ArrayRef<mlir::NamedAttribute>) { return nullptr; }
