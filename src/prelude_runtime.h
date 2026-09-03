#pragma once

#include <optional>
#include <span>

#include "joggle/diagnostic.h"
#include "joggle/module.h"
#include "joggle/type.h"

namespace joggle::detail {

bool is_prelude_primitive(const Module::FunctionDecl& function);

std::optional<ParameterValue> evaluate_prelude_primitive(
    const Module::FunctionDecl& function,
    std::span<const ParameterValue> arguments, Diagnostics& diagnostics,
    std::optional<SourceRange> source = std::nullopt);

}  // namespace joggle::detail
