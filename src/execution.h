#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "domain.h"
#include "joggle/compiler.h"

namespace joggle::detail {

struct FunctionBody;

std::string_view execution_value_type(const ExecutionValue& value);
std::optional<Domain> cpp_value_domain(std::string_view type);

std::optional<ExecutionValue>
execution_value(const ParameterValue& value,
                const Module::ParameterDecl& parameter);
std::optional<ParameterValue>
parameter_value(const ExecutionValue& value);

using ExecuteFunction = std::function<std::optional<ExecutionValue>(
    Module::FunctionDecl, std::vector<ExecutionValue>)>;

std::optional<ExecutionValue> execute_body(
    Compiler& compiler, const Module::FunctionDecl& function,
    const FunctionBody& body, std::span<const ExecutionValue> arguments,
    Compiler::EvaluationLimits limits, std::size_t& steps,
    bool under_residual_control, Diagnostics& diagnostics,
    const ExecuteFunction& execute);

}  // namespace joggle::detail
