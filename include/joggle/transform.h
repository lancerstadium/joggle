#pragma once

#include <cstddef>
#include <optional>

#include "joggle/diag.h"
#include "joggle/ir.h"
#include "joggle/mod.h"

namespace joggle {

// Inlines every source-defined or anonymous single-block call visible in the
// input snapshot, including calls in existing nested callable bodies. Newly
// cloned calls are left for a later invocation. Opaque, dynamic, and CFG
// callees are preserved. Each Fn edit is transactional.
std::optional<std::size_t> inline_calls(Compiler& compiler, Fn& fn,
                                        Diag& diagnostics);
std::optional<std::size_t> inline_calls(Compiler& compiler, Mod& mod,
                                        Diag& diagnostics);

// Applies the ordinary two-result body fns in `laws` as ordered equations.
// Each law returns its left and right expression from one shared set of typed
// arguments. Generic laws are specialized against candidate result Types
// before structural matching. Dead expressions are removed after replacement;
// effectful and control-flow equations are rejected.
std::optional<std::size_t> apply_pass(Compiler& compiler, Fn& fn,
                                      const Mod& laws, Diag& diagnostics);

}  // namespace joggle
