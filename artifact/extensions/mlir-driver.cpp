#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

llvm::json::Object analyze(mlir::ModuleOp module);

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  mlir::MLIRContext context(mlir::MLIRContext::Threading::DISABLED);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[1], &context);
  if (!module || mlir::failed(mlir::verify(*module)))
    return 3;
  llvm::outs() << llvm::json::Value(analyze(*module)) << '\n';
}
