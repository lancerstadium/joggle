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

// Applies one concrete, typed, single-result equation to every non-overlapping
// pure expression in `fn`. The arguments of `before` are pattern variables;
// `after` must have the same argument and result Types. Dead expressions are
// removed after replacement. Effectful and control-flow rules are rejected.
std::optional<std::size_t> apply_pass(Compiler& compiler, Fn& fn,
                                      const Fn& before, const Fn& after,
                                      Diag& diagnostics);

}  // namespace joggle
