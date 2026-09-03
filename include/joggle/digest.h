#pragma once

#include <string>
#include <string_view>

namespace joggle {

// Returns the lowercase hexadecimal SHA-256 digest of the exact input bytes.
// std::string_view is length-aware, so embedded zero bytes are significant.
std::string sha256(std::string_view input);

}  // namespace joggle
