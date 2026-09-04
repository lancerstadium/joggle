#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "joggle/compiler.h"

namespace joggle::detail {

bool check_declaration_expression(
    const Compiler& compiler, const Mod& scope, const Mod::Expr& expression,
    const Mod::Expr& expected,
    std::span<const Mod::FnDecl::GenericDecl> generics,
    std::span<const Mod::ParamDecl> locals, Diagnostics& diagnostics,
    std::optional<SourceRange> source, std::string_view subject);

}  // namespace joggle::detail
