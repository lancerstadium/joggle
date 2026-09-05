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
    std::span<const Mod::ParamDecl> locals, Diag& diagnostics,
    std::optional<Loc> source, std::string_view subject,
    bool public_contract = false);

}  // namespace joggle::detail
