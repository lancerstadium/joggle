#pragma once

#include <cstddef>
#include <optional>

#include "joggle/diag.h"
#include "joggle/ir.h"
#include "joggle/mod.h"

namespace joggle {

// Replaces every maximal non-overlapping occurrence of before with after in
// one transaction. The low-level overload checks IR safety but not semantic
// equivalence. Zero changes are a successful no-op.
std::optional<std::size_t> replace(Fn& fn, const Fn& before, const Fn& after,
                                   Diag& diagnostics);

// Applies the same transaction to every materialized member of a Mod.
std::optional<std::size_t> replace(Mod& mod, const Fn& before, const Fn& after,
                                   Diag& diagnostics);

// Proves conservative definitional equivalence by expanding eligible source
// bodies. Opaque calls remain exact-identity leaves.
bool equivalent(Compiler& compiler, const Fn& left, const Fn& right,
                Diag& diagnostics, std::size_t max_expansions = 256U);

// Checks definitional equivalence before opening the replacement transaction.
std::optional<std::size_t> replace(Compiler& compiler, Fn& fn, const Fn& before,
                                   const Fn& after, Diag& diagnostics,
                                   std::size_t max_expansions = 256U);

std::optional<std::size_t> replace(Compiler& compiler, Mod& mod,
                                   const Fn& before, const Fn& after,
                                   Diag& diagnostics,
                                   std::size_t max_expansions = 256U);

}  // namespace joggle
