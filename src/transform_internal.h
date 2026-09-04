#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "joggle/diag.h"
#include "joggle/ir.h"

namespace joggle::detail {

bool validate_expression_template(const Fn& fn, std::string_view role,
                                  Diag& diagnostics);

// A transient view over subject handles. It owns no graph or pattern state.
struct ExprMatch {
  Val root;
  std::vector<Val> bindings;
  std::vector<Op> calls;
};

std::optional<std::vector<ExprMatch>>
match_expressions(const Fn& subject, const Fn& pattern, Diag& diagnostics);

std::optional<std::size_t>
replace_expressions(Fn& subject, const Fn& before, const Fn& after,
                    Diag& diagnostics, std::span<const Val> allowed_roots = {});

}  // namespace joggle::detail
