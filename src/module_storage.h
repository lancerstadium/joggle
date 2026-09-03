#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "function_body.h"
#include "module_internal.h"

namespace joggle::detail {

struct MethodDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> inputs;
  std::vector<Module::ParameterDecl> results;
};

struct InterfaceDefinition {
  std::string name;
  Module::SymbolKind subject = Module::SymbolKind::Type;
  std::vector<Module::ParameterDecl> fields;
  std::vector<MethodDefinition> methods;
  std::optional<SourceRange> source;
};

struct TypeDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> parameters;
  std::vector<std::string> interfaces;
  std::vector<Module::TypeDecl::DerivedParameterDecl> derived_parameters;
  std::optional<SourceRange> source;
};

struct AttributeDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> parameters;
  std::vector<std::string> interfaces;
  std::optional<SourceRange> source;
};

struct FunctionDefinition {
  std::string name;
  std::vector<Module::FunctionDecl::GenericDecl> generics;
  std::vector<Module::ParameterDecl> inputs;
  std::vector<Module::ParameterDecl> results;
  FunctionTypeContract types;
  std::vector<std::string> interfaces;
  std::optional<std::string> operator_symbol;
  std::optional<Module::FunctionDecl::Fixity> operator_fixity;
  std::optional<FunctionBody> body;
  std::optional<SourceRange> source;
};

}  // namespace joggle::detail

struct joggle::Module::Storage {
  std::string name;
  Version version;
  mutable std::string digest;
  std::vector<Import> imports;
  std::vector<SourceRange> import_sources;
  std::vector<detail::InterfaceDefinition> interfaces;
  std::vector<detail::TypeDefinition> types;
  std::vector<detail::AttributeDefinition> attributes;
  std::vector<detail::FunctionDefinition> functions;
  std::map<std::string, std::shared_ptr<ir::Function>, std::less<>>
      materialized_functions;
  mutable std::vector<std::pair<std::string, ir::Function::Revision>>
      digest_revisions;
};
