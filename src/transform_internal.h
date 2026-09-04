#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"

namespace joggle::detail {

bool validate_expression_template(const Function& function,
                                  std::string_view role,
                                  Diagnostics& diagnostics);

// A transient view over subject handles. It owns no graph or pattern state.
struct ExpressionMatch {
  Value root;
  std::vector<Value> bindings;
  std::vector<Op> calls;
};

std::optional<std::vector<ExpressionMatch>>
match_expressions(const Function& subject, const Function& pattern,
                  Diagnostics& diagnostics);

std::optional<std::size_t>
replace_expressions(Function& subject, const Function& before,
                    const Function& after, Diagnostics& diagnostics);

}  // namespace joggle::detail
