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

struct FunctionBody;

using TypeExpression = Module::Expression;

using GenericDefinition = Module::Function::GenericDecl;

struct FunctionTypeContract {
  std::vector<GenericDefinition> generics;
  // Entries align with Function::inputs(). A parameter input carries an
  // expression when its value binds a generic; IR inputs have no binding.
  std::vector<std::optional<TypeExpression>> bindings;
  // Transitional implementation detail. Public parameters have one form;
  // these bits only record which values the current IR bridge can represent
  // as SSA values. Staged arguments will replace this split.
  std::vector<bool> ir_inputs;
  std::vector<bool> ir_results;
};

struct FunctionTypeAccess {
  static const FunctionTypeContract& get(const Module::Function&);
  static std::vector<Module::ParameterDecl>
  parameter_inputs(const Module::Function&);
  static std::vector<Module::ParameterDecl>
  ir_inputs(const Module::Function&);
  static std::vector<Module::ParameterDecl>
  parameter_results(const Module::Function&);
  static std::vector<Module::ParameterDecl>
  ir_results(const Module::Function&);
};

inline std::vector<Module::ParameterDecl>
parameter_inputs(const Module::Function& function) {
  return FunctionTypeAccess::parameter_inputs(function);
}

inline std::vector<Module::ParameterDecl>
ir_inputs(const Module::Function& function) {
  return FunctionTypeAccess::ir_inputs(function);
}

inline std::vector<Module::ParameterDecl>
parameter_results(const Module::Function& function) {
  return FunctionTypeAccess::parameter_results(function);
}

inline std::vector<Module::ParameterDecl>
ir_results(const Module::Function& function) {
  return FunctionTypeAccess::ir_results(function);
}

// True when a body can be instantiated without caller-supplied Known values
// or module-type context. Module validation uses this concrete default
// specialization in addition to declaration-level generic checking.
bool has_default_specialization(const Module::Function& function);

struct ModuleAccess {
  static std::shared_ptr<const FunctionBody> body(const Module& module,
                                                  const Module::Function&);
  static const Module::Expression*
  expression(const Module::Function& function);
  static const Module::Expression*
  returned_expression(const Module::Function& function);
  static std::optional<SourceRange> import_source(const Module& module,
                                                  std::size_t index);
  static std::optional<SourceRange> declaration_source(const Module& module,
                                                       Module::SymbolKind kind,
                                                       std::string_view name);
};

}  // namespace joggle::detail
