#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#ifdef EXTENSION_DEFINITION
void registerExtension(mlir::MLIRContext &context);
#elif defined(EXTENSION_REWRITE)
void transform(mlir::ModuleOp module);
#else
llvm::json::Object analyze(mlir::ModuleOp module);
#endif

int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  mlir::MLIRContext context(mlir::MLIRContext::Threading::DISABLED);
  context.loadDialect<mlir::func::FuncDialect>();
#ifdef EXTENSION_DEFINITION
  registerExtension(context);
#endif
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[1], &context);
  if (!module || mlir::failed(mlir::verify(*module)))
    return 3;
#ifdef EXTENSION_DEFINITION
  std::string printed;
  llvm::raw_string_ostream stream(printed);
  module->print(stream);
  auto reparsed = mlir::parseSourceString<mlir::ModuleOp>(printed, &context);
  if (!reparsed || mlir::failed(mlir::verify(*reparsed))) return 4;
  auto subject = reparsed->lookupSymbol<mlir::func::FuncOp>("subject");
  if (!subject) return 5;
  llvm::json::Array types;
  for (auto type : subject.getArgumentTypes()) {
    std::string text;
    llvm::raw_string_ostream output(text);
    type.print(output);
    types.push_back(text);
  }
  llvm::outs() << llvm::json::Value(llvm::json::Object{{"types", std::move(types)}}) << '\n';
#elif defined(EXTENSION_REWRITE)
  transform(*module);
  if (mlir::failed(mlir::verify(*module)))
    return 4;
  module->print(llvm::outs());
  llvm::outs() << '\n';
#else
  llvm::outs() << llvm::json::Value(analyze(*module)) << '\n';
#endif
}
