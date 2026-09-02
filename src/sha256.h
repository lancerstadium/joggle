#pragma once

#include <string>
#include <string_view>

namespace joggle::detail {

std::string sha256(std::string_view input);

}  // namespace joggle::detail
