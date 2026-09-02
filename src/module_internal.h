#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "joggle/module.h"

namespace joggle::detail {

struct GraphSyntax;

struct TermDefinition {
  enum class Kind { Variable, Operation };
  Kind kind = Kind::Variable;
  std::string name;
  std::vector<std::size_t> arguments;
};

struct RuleDefinition {
  std::vector<TermDefinition> terms;
  std::size_t match = 0;
  std::size_t replacement = 0;
  std::optional<SourceRange> source;
};

struct TypeExpression {
  enum class Kind { Number, Boolean, String, List, Reference, Variable };
  Kind kind = Kind::Number;
  std::string text;
  std::vector<TypeExpression> arguments;
};

struct GenericDefinition {
  std::string name;
  Module::ParameterKind kind = Module::ParameterKind::Type;
  bool list = false;
};

struct OperationTypeContract {
  std::vector<GenericDefinition> generics;
  // Entries align with OperationDecl::inputs(). Value operands carry their
  // type expression. A named property carries an expression only when its
  // value binds an operation generic.
  std::vector<std::optional<TypeExpression>> inputs;
  // Entries align with OperationDecl::results().
  std::vector<TypeExpression> results;
};

struct OperationTypeAccess {
  static const OperationTypeContract& get(const Module::OperationDecl&);
};

struct ModuleAccess {
  static std::shared_ptr<const GraphSyntax> graph(const Module& module,
                                                  std::string_view name);
  static std::optional<SourceRange> import_source(const Module& module,
                                                  std::size_t index);
  static std::optional<SourceRange> declaration_source(const Module& module,
                                                       Module::SymbolKind kind,
                                                       std::string_view name);
  static std::span<const RuleDefinition> rules(const Module& module,
                                               const Module::PassDecl& pass);
};

}  // namespace joggle::detail
