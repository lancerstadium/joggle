#pragma once

#include <cstddef>
#include <optional>

#include "joggle/diag.h"
#include "joggle/ir.h"
#include "joggle/mod.h"

namespace joggle {

// Inlines every source-defined or anonymous single-block call visible in the
// input snapshot. Newly cloned calls are left for a later invocation. Opaque,
// dynamic, and CFG callees are preserved. The whole edit is transactional.
std::optional<std::size_t> inline_calls(Compiler& compiler, Fn& fn,
                                        Diag& diagnostics);
std::optional<std::size_t> inline_calls(Compiler& compiler, Mod& mod,
                                        Diag& diagnostics);

}  // namespace joggle
