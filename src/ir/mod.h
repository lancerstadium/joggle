#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "joggle/mod.h"

namespace joggle::detail {

struct FnBody;

using TypeExpr = Mod::Expr;

using GenericDefinition = Mod::FnDecl::GenericDecl;

struct FnTypeContract {
  std::vector<GenericDefinition> generics;
  // Entries align with Fn::inputs(). A compiler-domain input carries an
  // expression only when its value binds a generic.
  std::vector<std::optional<TypeExpr>> bindings;
};

// A port is a mod value exactly when its declared domain is not one of the
// compiler domains. This is derived from the declaration and never stored as
// a parallel signature.
bool is_value_port(const Mod::ParamDecl& parameter);

struct FnTypeAccess {
  static const FnTypeContract& get(const Mod::FnDecl&);
  static std::vector<Mod::ParamDecl> compiler_inputs(const Mod::FnDecl&);
  static std::vector<Mod::ParamDecl> value_inputs(const Mod::FnDecl&);
  static std::vector<Mod::ParamDecl> compiler_results(const Mod::FnDecl&);
  static std::vector<Mod::ParamDecl> value_results(const Mod::FnDecl&);
};

inline std::vector<Mod::ParamDecl> compiler_inputs(const Mod::FnDecl& fn) {
  return FnTypeAccess::compiler_inputs(fn);
}

inline std::vector<Mod::ParamDecl> value_inputs(const Mod::FnDecl& fn) {
  return FnTypeAccess::value_inputs(fn);
}

inline std::vector<Mod::ParamDecl> compiler_results(const Mod::FnDecl& fn) {
  return FnTypeAccess::compiler_results(fn);
}

inline std::vector<Mod::ParamDecl> value_results(const Mod::FnDecl& fn) {
  return FnTypeAccess::value_results(fn);
}

// True when a body can be instantiated without caller-supplied Known values
// or mod-type context. Mod validation uses this concrete default
// specialization in addition to declaration-level generic checking.
bool has_default_specialization(const Mod::FnDecl& fn);

struct ModAccess {
  static Mod declaration_view(const Mod& mod);
  static std::shared_ptr<const FnBody> body(const Mod& mod, const Mod::FnDecl&);
  static const Mod::Expr* expression(const Mod::FnDecl& fn);
  static const Mod::Expr* returned_expression(const Mod::FnDecl& fn);
  static std::optional<Loc> import_source(const Mod& mod, std::size_t index);
  static std::optional<Loc> declaration_source(const Mod& mod,
                                               Mod::Symbol::Kind kind,
                                               std::string_view name);
};

}  // namespace joggle::detail
