#pragma once

#include <vector>

#include "joggle/ir.h"

namespace joggle::detail {

// Returns each reachable inline callable value in one Fn exactly once. Nested
// bodies are transformed separately; this view follows only capture edges that
// belong to the current owner Fn.
std::vector<Val> nested_values(const Fn& fn);

}  // namespace joggle::detail
