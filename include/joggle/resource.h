#pragma once

#include <map>
#include <string>

#include "joggle/compiler.h"

namespace joggle {

// Detached payloads carried explicitly beside a Module. Producers choose the
// resource names; content-addressed producers conventionally use sha256:<hex>.
using ResourceSet = std::map<std::string, Bytes, std::less<>>;

}  // namespace joggle
