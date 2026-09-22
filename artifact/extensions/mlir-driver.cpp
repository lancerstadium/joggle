#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#ifdef EXTENSION_REWRITE
void transform(mlir::ModuleOp module);
#else
llvm::json::Object analyze(mlir::ModuleOp module);
#endif

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  mlir::MLIRContext context(mlir::MLIRContext::Threading::DISABLED);
  context.loadDialect<mlir::func::FuncDialect>();
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[1], &context);
  if (!module || mlir::failed(mlir::verify(*module)))
    return 3;
#ifdef EXTENSION_REWRITE
  transform(*module);
  if (mlir::failed(mlir::verify(*module)))
    return 4;
  module->print(llvm::outs());
  llvm::outs() << '\n';
#else
  llvm::outs() << llvm::json::Value(analyze(*module)) << '\n';
#endif
}
