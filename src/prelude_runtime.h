#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "joggle/diagnostic.h"
#include "joggle/mod.h"
#include "joggle/type.h"

namespace joggle::detail {

bool is_prelude_primitive(const Mod::FnDecl& fn);

std::optional<ParamVal>
evaluate_prelude_primitive(const Mod::FnDecl& fn,
                           std::span<const ParamVal> arguments,
                           Diagnostics& diagnostics, std::size_t element_limit,
                           std::optional<SourceRange> source = std::nullopt);

}  // namespace joggle::detail
