#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <exception>

#ifdef EXTENSION_COMPOUND
void transform(mlir::ModuleOp module);
llvm::json::Object analyze(mlir::ModuleOp module);
#elif defined(EXTENSION_INPUT_FORMAT)
mlir::OwningOpRef<mlir::ModuleOp> readExtension(llvm::StringRef source,
                                               mlir::MLIRContext &context);
#elif defined(EXTENSION_DEFINITION)
void registerExtension(mlir::MLIRContext &context);
#ifdef EXTENSION_CONSTRUCT
mlir::Operation *constructExtension(mlir::OpBuilder &builder, mlir::ValueRange operands,
                                    llvm::ArrayRef<mlir::NamedAttribute> attributes);
#endif
#elif defined(EXTENSION_REWRITE)
void transform(mlir::ModuleOp module);
#else
llvm::json::Object analyze(mlir::ModuleOp module);
#endif

int main(int argc, char **argv) {
#ifdef EXTENSION_COMPOUND
  const bool emit = argc == 3 && llvm::StringRef(argv[2]) == "--emit";
  if (argc != 2 && !emit)
    return 2;
#else
  if (argc != 2)
    return 2;
#endif
  mlir::MLIRContext context(mlir::MLIRContext::Threading::DISABLED);
  context.loadDialect<mlir::func::FuncDialect>();
#ifdef EXTENSION_INPUT_FORMAT
  auto input = llvm::MemoryBuffer::getFile(argv[1]);
  if (!input) return 2;
  try {
    auto module = readExtension((*input)->getBuffer(), context);
    if (!module || mlir::failed(mlir::verify(*module))) return 3;
    std::string first;
    llvm::raw_string_ostream output(first);
    module->print(output);
    auto reparsed = mlir::parseSourceString<mlir::ModuleOp>(first, &context);
    if (!reparsed || mlir::failed(mlir::verify(*reparsed))) return 4;
    std::string second;
    llvm::raw_string_ostream roundtrip(second);
    reparsed->print(roundtrip);
    if (first != second) return 4;
    llvm::outs() << second << '\n';
    return 0;
  } catch (const std::exception &error) {
    llvm::errs() << error.what() << '\n';
    return 3;
  }
#else
#ifdef EXTENSION_DEFINITION
  registerExtension(context);
#endif
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(argv[1], &context);
  if (!module || mlir::failed(mlir::verify(*module)))
    return 3;
#ifdef EXTENSION_COMPOUND
  if (emit) {
    llvm::outs() << llvm::json::Value(analyze(*module)) << '\n';
    return 0;
  }
#endif
#ifdef EXTENSION_DEFINITION
#ifdef EXTENSION_CONSTRUCT
  auto input = module->lookupSymbol<mlir::func::FuncOp>("subject");
  auto attributes = (*module)->getAttrOfType<mlir::DictionaryAttr>("study.attributes");
  if (!input || !attributes || !llvm::hasSingleElement(input.getBody())) return 5;
  auto *terminator = input.getBody().front().getTerminator();
  mlir::OpBuilder builder(terminator);
  auto *created = constructExtension(builder, input.getArguments(), attributes.getValue());
  if (!created || !created->getRegisteredInfo() || created->getBlock() != terminator->getBlock() ||
      created->getNumResults() != 1) return 6;
  mlir::func::ReturnOp::create(builder, terminator->getLoc(), created->getResults());
  terminator->erase();
  input.setType(builder.getFunctionType(input.getArgumentTypes(), created->getResultTypes()));
  if (mlir::failed(mlir::verify(*module))) return 3;
#endif
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
#ifdef EXTENSION_CONSTRUCT
  reparsed->print(llvm::outs());
  llvm::outs() << '\n';
#else
  llvm::outs() << llvm::json::Value(llvm::json::Object{{"types", std::move(types)}}) << '\n';
#endif
#elif defined(EXTENSION_REWRITE) || defined(EXTENSION_COMPOUND)
  transform(*module);
  if (mlir::failed(mlir::verify(*module)))
    return 4;
  module->print(llvm::outs());
  llvm::outs() << '\n';
#else
  llvm::outs() << llvm::json::Value(analyze(*module)) << '\n';
#endif
#endif
}
