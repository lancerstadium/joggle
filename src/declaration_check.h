#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "joggle/compiler.h"

namespace joggle::detail {

bool check_generic_constraints(
    const Compiler& compiler, const Module& scope,
    std::span<const Module::Function::GenericDecl> generics,
    Diagnostics& diagnostics, std::optional<SourceRange> source,
    std::string_view subject);

bool check_declaration_expression(
    const Compiler& compiler, const Module& scope,
    const Module::Expression& expression, const Module::Expression& expected,
    std::span<const Module::Function::GenericDecl> generics,
    std::span<const Module::ParameterDecl> locals, Diagnostics& diagnostics,
    std::optional<SourceRange> source, std::string_view subject);

}  // namespace joggle::detail
