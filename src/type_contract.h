#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "execution.h"

namespace joggle::detail {

struct CallTypes {
  std::vector<Type> arguments;
  std::vector<Type> results;
};

std::optional<ParameterValue> evaluate_known_expression(
    Compiler& compiler, std::string_view scope,
    const Module::Expression& expression,
    const Module::ParameterDecl& expected, const KnownBindings& bindings,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt,
    bool allow_host_evaluation = true);

std::optional<std::vector<ParameterValue>> resolve_derived_parameters(
    Compiler& compiler, const Module::TypeDecl& schema,
    std::span<const ParameterValue> parameters, Diagnostics& diagnostics);

std::optional<std::vector<ParameterValue>> resolve_derived_parameters(
    std::span<const Module> modules, const Module::TypeDecl& schema,
    std::span<const ParameterValue> parameters, Diagnostics& diagnostics);

std::optional<CallTypes> resolve_call_types(
    Compiler& compiler, const Module::Function& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

std::optional<CallTypes> resolve_partial_call_types(
    Compiler& compiler, const Module::Function& schema,
    std::span<const std::optional<Type>> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt,
    bool allow_host_evaluation = true);

std::optional<CallTypes> resolve_call_types(
    std::span<const Module> modules, const Module::Function& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

std::optional<std::vector<Type>>
infer_call_types(Compiler& compiler, const Module::Function& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParameterValue>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diagnostics& diagnostics,
                 std::optional<SourceRange> source = std::nullopt);

std::optional<std::vector<Type>> infer_call_types(
    std::span<const Module> modules, const Module::Function& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

}  // namespace joggle::detail
