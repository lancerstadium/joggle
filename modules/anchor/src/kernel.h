#pragma once

#include <optional>

#include <joggle/joggle.h>

namespace joggle::anchor {

std::optional<Bytes> kernel_report(Compiler& compiler, const Module& program,
                                   Diagnostics& diagnostics);

}  // namespace joggle::anchor
