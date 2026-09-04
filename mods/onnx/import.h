#pragma once

#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace joggle_onnx {

std::optional<joggle::Mod> read(joggle::Compiler& compiler,
                                const joggle::Bytes& input, std::string name,
                                joggle::Diag& diagnostics);

}  // namespace joggle_onnx
