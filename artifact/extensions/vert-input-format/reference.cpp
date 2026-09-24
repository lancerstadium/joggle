#include "mlir/IR/Builders.h"
#include "mlir/AsmParser/AsmParser.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Parser/Parser.h"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

mlir::OwningOpRef<mlir::ModuleOp> readExtension(llvm::StringRef source,
                                               mlir::MLIRContext &context) {
  mlir::OpBuilder builder(&context);
  auto location = builder.getUnknownLoc();
  mlir::OwningOpRef<mlir::ModuleOp> module(mlir::ModuleOp::create(location));
  auto subject = mlir::func::FuncOp::create(location, "subject", builder.getFunctionType({}, {}));
  module->push_back(subject);
  auto *body = subject.addEntryBlock();
  builder.setInsertionPointToEnd(body);
  std::map<std::string, mlir::Value> values;
  std::map<std::string, std::string> symbols;
  bool started = false, returned = false;
  mlir::Type output;
  const std::regex identifier("%[A-Za-z_][A-Za-z_0-9]*");
  const std::regex tensorPattern("tensor<([0-9]+x)*(f32|f64|i8|i16|i32|i64)>");
  const std::regex numeric("[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)([eE][+-]?[0-9]+)?");
  auto name = [&](const std::string &token) {
    if (!std::regex_match(token, identifier)) throw std::runtime_error("syntax-error");
    return token.substr(1);
  };
  auto type = [&](const std::string &token) {
    if (!std::regex_match(token, tensorPattern)) throw std::runtime_error("syntax-error");
    auto parsed = mlir::dyn_cast_or_null<mlir::RankedTensorType>(mlir::parseType(token, &context));
    if (!parsed) throw std::runtime_error("syntax-error");
    return parsed;
  };
  auto lookup = [&](const std::string &token) {
    auto found = values.find(name(token));
    if (found == values.end()) throw std::runtime_error("undefined-value");
    return found->second;
  };
  std::istringstream lines(source.str());
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
    if (returned) throw std::runtime_error("syntax-error");
    // Tokenize '=' separately, including compact assignments.
    const auto equal = line.find('=');
    if (equal != std::string::npos) line.replace(equal, 1, " = ");
    std::istringstream words(line);
    std::vector<std::string> tokens;
    for (std::string word; words >> word;) tokens.push_back(word);
    if (tokens[0] == "return") {
      if (tokens.size() != 2) throw std::runtime_error("syntax-error");
      auto value = lookup(tokens[1]);
      mlir::func::ReturnOp::create(builder, location, value);
      output = value.getType();
      returned = true;
      continue;
    }
    if (tokens[0] == "input") {
      if (tokens.size() != 3 || started) throw std::runtime_error("syntax-error");
      auto key = name(tokens[1]);
      if (values.count(key)) throw std::runtime_error("duplicate-value");
      values[key] = body->addArgument(type(tokens[2]), location);
      continue;
    }
    if (tokens.size() < 4 || tokens[1] != "=") throw std::runtime_error("syntax-error");
    auto key = name(tokens[0]);
    if (values.count(key)) throw std::runtime_error("duplicate-value");
    std::string operation = tokens[2];
    llvm::SmallVector<mlir::Value> operands;
    mlir::RankedTensorType resultType;
    mlir::Attribute constant;
    if (operation == "const" && tokens.size() == 5) {
      resultType = type(tokens[4]);
      auto element = resultType.getElementType();
      if (auto integer = mlir::dyn_cast<mlir::IntegerType>(element)) {
        if (!std::regex_match(tokens[3], std::regex("[+-]?[0-9]+")))
          throw std::runtime_error("type-error");
        int64_t value;
        try { value = std::stoll(tokens[3]); }
        catch (const std::exception &) { throw std::runtime_error("literal-out-of-range"); }
        const auto width = integer.getWidth();
        if (width < 64 && (value < -(int64_t{1} << (width - 1)) || value >= (int64_t{1} << (width - 1))))
          throw std::runtime_error("literal-out-of-range");
        constant = builder.getIntegerAttr(integer, value);
      } else {
        if (!std::regex_match(tokens[3], numeric)) throw std::runtime_error("syntax-error");
        // IEEE underflow is a rounded finite value, not an invalid literal.
        // The validated grammar excludes partial parses and nonnumeric forms.
        double value = std::strtod(tokens[3].c_str(), nullptr);
        if (element.isF32()) value = static_cast<float>(value);
        if (!std::isfinite(value)) throw std::runtime_error("literal-out-of-range");
        constant = builder.getFloatAttr(element, value);
      }
      operation = "splat";
    } else if ((operation == "relu" && tokens.size() == 4) ||
               ((operation == "add" || operation == "mul") && tokens.size() == 5)) {
      for (std::size_t i = 3; i < tokens.size(); ++i) operands.push_back(lookup(tokens[i]));
      resultType = mlir::cast<mlir::RankedTensorType>(operands.front().getType());
      for (auto value : operands)
        if (value.getType() != resultType) throw std::runtime_error("type-error");
    } else { throw std::runtime_error("syntax-error"); }
    llvm::SmallVector<mlir::Type> argumentTypes;
    for (auto value : operands) argumentTypes.push_back(value.getType());
    auto signature = builder.getFunctionType(argumentTypes, {resultType});
    std::string signatureText;
    llvm::raw_string_ostream stream(signatureText);
    signature.print(stream);
    auto signatureKey = operation + signatureText;
    if (!symbols.count(signatureKey)) {
      auto symbol = operation + "__type" + std::to_string(symbols.size());
      symbols[signatureKey] = symbol;
      auto declaration = mlir::func::FuncOp::create(location, symbol, signature);
      declaration.setPrivate();
      module->push_back(declaration);
    }
    auto call = mlir::func::CallOp::create(builder, location, symbols[signatureKey],
                                         mlir::TypeRange{resultType}, operands);
    if (constant) call->setAttr("value", constant);
    values[key] = call.getResult(0);
    started = true;
  }
  if (!returned) throw std::runtime_error("syntax-error");
  subject.setType(builder.getFunctionType(body->getArgumentTypes(), {output}));
  return module;
}
