#include "prelude.h"

#include <array>

namespace joggle::detail {

bool is_prelude_type(std::string_view name) {
  constexpr std::array names{
      std::string_view{"type"},     std::string_view{"int"},
      std::string_view{"real"},     std::string_view{"bool"},
      std::string_view{"string"},   std::string_view{"attr"},
      std::string_view{"bytes"},    std::string_view{"function"},
      std::string_view{"callable"}, std::string_view{"list"},
      std::string_view{"i1"},
      std::string_view{"i8"},       std::string_view{"i16"},
      std::string_view{"i32"},      std::string_view{"i64"},
      std::string_view{"u8"},       std::string_view{"u16"},
      std::string_view{"u32"},      std::string_view{"u64"},
      std::string_view{"f16"},      std::string_view{"bf16"},
      std::string_view{"f32"},      std::string_view{"f64"},
      std::string_view{"index"},
  };
  for (const std::string_view candidate : names) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

std::string_view display_type_name(std::string_view qualified_name) {
  constexpr std::string_view prefix = "prelude.";
  if (qualified_name.starts_with(prefix)) {
    const std::string_view local = qualified_name.substr(prefix.size());
    if (is_prelude_type(local)) {
      return local;
    }
  }
  return qualified_name;
}

}  // namespace joggle::detail
