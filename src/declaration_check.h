#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "joggle/compiler.h"

namespace joggle::detail {

bool check_declaration_expression(
    const Compiler& compiler, const Module& scope,
    const Module::Expression& expression, const Module::Expression& expected,
    std::span<const Module::FunctionDecl::GenericDecl> generics,
    std::span<const Module::ParameterDecl> locals, Diagnostics& diagnostics,
    std::optional<SourceRange> source, std::string_view subject);

}  // namespace joggle::detail
