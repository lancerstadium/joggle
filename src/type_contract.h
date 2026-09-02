#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "joggle/compiler.h"

namespace joggle::detail {

using KnownBindings = std::unordered_map<std::string, ParameterValue>;

struct OperationTypes {
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

std::optional<OperationTypes>
resolve_operation_types(Compiler& compiler,
                        const Module::FunctionDecl& schema,
                        std::span<const Type> arguments,
                        std::span<const std::optional<ParameterValue>> known_arguments,
                        std::span<const std::optional<Type>> expected_results,
                        Diagnostics& diagnostics,
                        std::optional<SourceRange> source = std::nullopt);

std::optional<OperationTypes> resolve_operation_types(
    std::span<const Module> modules, const Module::FunctionDecl& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

std::optional<std::vector<Type>>
infer_operation_types(Compiler& compiler, const Module::FunctionDecl& schema,
                      std::span<const Type> arguments,
                      std::span<const std::optional<ParameterValue>> known_arguments,
                      std::span<const std::optional<Type>> expected_results,
                      Diagnostics& diagnostics,
                      std::optional<SourceRange> source = std::nullopt);

std::optional<std::vector<Type>> infer_operation_types(
    std::span<const Module> modules, const Module::FunctionDecl& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

}  // namespace joggle::detail
