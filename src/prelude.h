#pragma once

#include <string_view>

namespace joggle {
class Type;
}

namespace joggle::detail {

inline constexpr std::string_view prelude_module_name = "prelude";

bool is_prelude_type(std::string_view name);
bool is_effect_type(const Type& type);
std::string_view prelude_module_source();
std::string_view display_type_name(std::string_view qualified_name);

}  // namespace joggle::detail
