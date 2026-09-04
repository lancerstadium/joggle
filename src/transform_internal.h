#pragma once

#include <string_view>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"

namespace joggle::detail {

bool validate_expression_template(const Function& function,
                                  std::string_view role,
                                  Diagnostics& diagnostics);

}  // namespace joggle::detail
