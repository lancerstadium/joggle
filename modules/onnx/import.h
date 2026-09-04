#pragma once

#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace joggle_onnx {

std::optional<joggle::Module>
read(joggle::Compiler& compiler, const joggle::Bytes& input, std::string name,
     joggle::Diagnostics& diagnostics);

}  // namespace joggle_onnx
