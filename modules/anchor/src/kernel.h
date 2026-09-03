#pragma once

#include <optional>

#include <joggle/joggle.h>

namespace joggle::anchor {

std::optional<Module> kernel_bundle(Compiler& compiler, const Module& program,
                                    Diagnostics& diagnostics);

std::optional<Bytes> kernel_report(Compiler& compiler, const Module& program,
                                   Diagnostics& diagnostics);

}  // namespace joggle::anchor
