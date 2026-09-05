#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "joggle/diag.h"
#include "joggle/ir.h"

namespace joggle::detail {

using ValMap = std::vector<std::pair<Val, Val>>;

// Clones one single-block body immediately before `before`. `values` supplies
// substitutions for arguments and may also contain values cloned earlier.
// The returned values are the cloned return operands.
std::optional<std::vector<Val>>
clone_before(Fn::Edit& edit, const Fn& source, Op before, ValMap& values,
             Diag& diagnostics, std::optional<Loc> location = std::nullopt);

// Clones only the pure expression slice required by `source`. Existing
// substitutions in `values` stop traversal, so equation arguments map directly
// to target values and an unused left side is never copied.
std::optional<Val> clone_before(Fn::Edit& edit, const Val& source, Op before,
                                ValMap& values, Diag& diagnostics,
                                std::optional<Loc> location = std::nullopt);

}  // namespace joggle::detail
