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
struct RuleDefinition;

using TypeExpression = Module::Expression;

using GenericDefinition = Module::FunctionDecl::GenericDecl;

struct FunctionTypeContract {
  std::vector<GenericDefinition> generics;
  // Entries align with FunctionDecl::inputs(). A static input carries an
  // expression when its value binds a generic; SSA inputs have no binding.
  std::vector<std::optional<TypeExpression>> bindings;
};

struct FunctionTypeAccess {
  static const FunctionTypeContract& get(const Module::FunctionDecl&);
};

struct ModuleAccess {
  static std::shared_ptr<const FunctionBody> body(
      const Module& module, const Module::FunctionDecl&);
  static const Module::Expression* expression(
      const Module::FunctionDecl& function);
  static std::optional<SourceRange> import_source(const Module& module,
                                                  std::size_t index);
  static std::optional<SourceRange> declaration_source(const Module& module,
                                                       Module::SymbolKind kind,
                                                       std::string_view name);
  static std::span<const RuleDefinition> rules(const Module& module,
                                               const Module::FunctionDecl&);
};

}  // namespace joggle::detail
