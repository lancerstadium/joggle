#pragma once

#include <optional>
#include <string_view>

#include <joggle/joggle.h>

namespace joggle::anchor {

std::optional<Bytes> pack_artifact(std::string_view manifest,
                                   const Module& bundle,
                                   Diagnostics& diagnostics);

std::optional<Module> unpack_artifact(Compiler& compiler,
                                      const Bytes& artifact,
                                      Diagnostics& diagnostics);

}  // namespace joggle::anchor
