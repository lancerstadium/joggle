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

using GenericDefinition = Module::FunctionDecl::GenericDecl;

struct FunctionTypeContract {
  std::vector<GenericDefinition> generics;
  // Entries align with Function::inputs(). A compiler-domain input carries an
  // expression only when its value binds a generic.
  std::vector<std::optional<TypeExpression>> bindings;
};

// A port is a module value exactly when its declared domain is not one of the
// compiler domains. This is derived from the declaration and never stored as
// a parallel signature.
bool is_value_port(const Module::ParameterDecl& parameter);

struct FunctionTypeAccess {
  static const FunctionTypeContract& get(const Module::FunctionDecl&);
  static std::vector<Module::ParameterDecl>
  compiler_inputs(const Module::FunctionDecl&);
  static std::vector<Module::ParameterDecl>
  value_inputs(const Module::FunctionDecl&);
  static std::vector<Module::ParameterDecl>
  compiler_results(const Module::FunctionDecl&);
  static std::vector<Module::ParameterDecl>
  value_results(const Module::FunctionDecl&);
};

inline std::vector<Module::ParameterDecl>
compiler_inputs(const Module::FunctionDecl& function) {
  return FunctionTypeAccess::compiler_inputs(function);
}

inline std::vector<Module::ParameterDecl>
value_inputs(const Module::FunctionDecl& function) {
  return FunctionTypeAccess::value_inputs(function);
}

inline std::vector<Module::ParameterDecl>
compiler_results(const Module::FunctionDecl& function) {
  return FunctionTypeAccess::compiler_results(function);
}

inline std::vector<Module::ParameterDecl>
value_results(const Module::FunctionDecl& function) {
  return FunctionTypeAccess::value_results(function);
}

// True when a body can be instantiated without caller-supplied Known values
// or module-type context. Module validation uses this concrete default
// specialization in addition to declaration-level generic checking.
bool has_default_specialization(const Module::FunctionDecl& function);

struct ModuleAccess {
  static Module declaration_view(const Module& module);
  static std::shared_ptr<const FunctionBody> body(const Module& module,
                                                  const Module::FunctionDecl&);
  static const Module::Expression*
  expression(const Module::FunctionDecl& function);
  static const Module::Expression*
  returned_expression(const Module::FunctionDecl& function);
  static std::optional<SourceRange> import_source(const Module& module,
                                                  std::size_t index);
  static std::optional<SourceRange> declaration_source(const Module& module,
                                                       Module::SymbolKind kind,
                                                       std::string_view name);
};

}  // namespace joggle::detail
