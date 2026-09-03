#pragma once

#include <map>
#include <string>

#include <joggle/compiler.h>

namespace joggle::onnx {

// Detached immutable tensor payloads keyed by their canonical resource name.
// The ordinary container alias keeps the extension ABI transparent: there is
// no resource manager, singleton, or second model object.
using Resources = std::map<std::string, Bytes, std::less<>>;

}  // namespace joggle::onnx
