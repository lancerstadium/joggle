#pragma once

#include <optional>

#include <joggle/joggle.h>

namespace joggle::anchor {

struct ExecutionSchema {
  Module target;
  Module::InterfaceDecl memory_reference;
  Module::InterfaceDecl placement;
};

std::optional<Bytes> execute_f32(Compiler& compiler, const Module& program,
                                 const Bytes& input,
                                 const ExecutionSchema& schema,
                                 Diagnostics& diagnostics);

}  // namespace joggle::anchor
