#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fn_body.h"
#include "mod_internal.h"

namespace joggle::detail {

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

// One Mod owns one ordered fn-member table. A parsed member initially
// has a declaration; a compiler-produced member initially has materialized IR.
// Keeping both states in the same entry lets linking/materialization converge
// without creating a second whole-mod container.
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
