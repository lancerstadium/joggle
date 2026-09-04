#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "compile/eval.h"

namespace joggle::detail {

struct CallTypes {
  std::vector<Type> arguments;
  std::vector<Type> results;
  KnownBindings bindings;
};

std::optional<ParamVal> evaluate_known_expression(
    Compiler& compiler, std::string_view scope, const Mod::Expr& expression,
    const Mod::ParamDecl& expected, const KnownBindings& bindings,
    Diag& diagnostics, std::optional<Loc> source = std::nullopt,
    bool allow_host_evaluation = true);

std::optional<std::vector<ParamVal>>
resolve_derived_parameters(Compiler& compiler, const Mod::TypeDecl& schema,
                           std::span<const ParamVal> parameters,
                           Diag& diagnostics);

std::optional<std::vector<ParamVal>> resolve_derived_parameters(
    std::span<const Mod> mods, const Mod::TypeDecl& schema,
    std::span<const ParamVal> parameters, Diag& diagnostics);

std::optional<CallTypes>
resolve_call_types(Compiler& compiler, const Mod::FnDecl& schema,
                   std::span<const Type> arguments,
                   std::span<const std::optional<ParamVal>> known_arguments,
                   std::span<const std::optional<Type>> expected_results,
                   Diag& diagnostics, std::optional<Loc> source = std::nullopt);

std::optional<CallTypes> resolve_partial_call_types(
    Compiler& compiler, const Mod::FnDecl& schema,
    std::span<const std::optional<Type>> arguments,
    std::span<const std::optional<ParamVal>> known_arguments,
    std::span<const std::optional<Type>> expected_results, Diag& diagnostics,
    std::optional<Loc> source = std::nullopt,
    bool allow_host_evaluation = true);

std::optional<CallTypes>
resolve_call_types(std::span<const Mod> mods, const Mod::FnDecl& schema,
                   std::span<const Type> arguments,
                   std::span<const std::optional<ParamVal>> known_arguments,
                   std::span<const std::optional<Type>> expected_results,
                   Diag& diagnostics, std::optional<Loc> source = std::nullopt);

std::optional<std::vector<Type>>
infer_call_types(Compiler& compiler, const Mod::FnDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParamVal>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diag& diagnostics, std::optional<Loc> source = std::nullopt);

std::optional<std::vector<Type>>
infer_call_types(std::span<const Mod> mods, const Mod::FnDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParamVal>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diag& diagnostics, std::optional<Loc> source = std::nullopt);

}  // namespace joggle::detail
