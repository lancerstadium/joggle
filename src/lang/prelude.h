#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "joggle/diag.h"
#include "joggle/mod.h"
#include "joggle/type.h"

namespace joggle::detail {

inline constexpr std::string_view prelude_mod_name = "prelude";

bool is_prelude_type(std::string_view name);
bool is_effect_type(const Type& type);
std::string_view prelude_mod_source();
std::string_view display_type_name(std::string_view qualified_name);

bool is_prelude_primitive(const Mod::FnDecl& fn);

std::optional<ParamVal>
evaluate_prelude_primitive(const Mod::FnDecl& fn,
                           std::span<const ParamVal> arguments,
                           Diag& diagnostics, std::size_t element_limit,
                           std::optional<Loc> source = std::nullopt);

}  // namespace joggle::detail
