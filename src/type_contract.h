#pragma once

#include <optional>
#include <span>
#include <vector>

#include "joggle/compiler.h"

namespace joggle::detail {

std::optional<std::vector<Type>>
infer_operation_types(Compiler& compiler, const Module::OperationDecl& schema,
                      std::span<const Type> operands,
                      std::span<const std::optional<ParameterValue>> properties,
                      std::span<const std::optional<Type>> expected_results,
                      Diagnostics& diagnostics,
                      std::optional<SourceRange> source = std::nullopt);

std::optional<std::vector<Type>> infer_operation_types(
    std::span<const Module> modules, const Module::OperationDecl& schema,
    std::span<const Type> operands,
    std::span<const std::optional<ParameterValue>> properties,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

}  // namespace joggle::detail
