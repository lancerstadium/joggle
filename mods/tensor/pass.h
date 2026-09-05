#pragma once

#include <optional>

#include <joggle/joggle.h>

namespace joggle_tensor {

std::optional<joggle::Fn> fuse(joggle::Compiler& compiler,
                               const joggle::Mod& tensor, joggle::Fn input,
                               joggle::Diag& diagnostics);

std::optional<joggle::Fn> loops(joggle::Compiler& compiler,
                                const joggle::Mod& tensor, joggle::Fn input,
                                joggle::Diag& diagnostics);

}  // namespace joggle_tensor
