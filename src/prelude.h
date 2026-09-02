#pragma once

#include <string_view>

namespace joggle::detail {

inline constexpr std::string_view prelude_module_name = "prelude";

bool is_prelude_type(std::string_view name);
std::string_view prelude_module_source();
std::string_view display_type_name(std::string_view qualified_name);

}  // namespace joggle::detail
