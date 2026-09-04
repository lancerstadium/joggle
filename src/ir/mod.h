#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "joggle/mod.h"
#include "lang/fn.h"

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

struct TypeDefinition {
  std::string name;
  std::vector<Mod::ParamDecl> parameters;
  std::vector<Mod::TypeDecl::DerivedParamDecl> derived_parameters;
  std::optional<Loc> source;
};

struct FnDef {
  std::string name;
  std::vector<Mod::FnDecl::GenericDecl> generics;
  std::vector<Mod::ParamDecl> inputs;
  std::vector<Mod::ParamDecl> results;
  FnTypeContract types;
  std::optional<Mod::FnDecl::Fixity> operator_fixity;
  std::optional<FnBody> body;
  std::optional<Loc> source;
};

// One Mod owns one ordered fn-member table. A parsed member initially has a
// declaration; a compiler-produced member initially has materialized IR.
// Keeping both states in the same entry lets linking and materialization
// converge without creating a second whole-mod container.
struct FnMember {
  std::string name;
  std::optional<FnDef> declaration;
  std::shared_ptr<Fn> ir;
};

}  // namespace joggle::detail

struct joggle::Mod::Storage {
  std::string name;
  Version version;
  mutable std::string digest;
  std::string declaration_digest;
  std::vector<Import> imports;
  std::vector<Loc> import_sources;
  std::vector<detail::TypeDefinition> types;
  std::vector<detail::FnMember> fns;
  std::map<std::string, std::shared_ptr<const Bytes>, std::less<>> data;
  mutable std::vector<std::pair<std::string, Fn::Revision>> digest_revisions;
};
