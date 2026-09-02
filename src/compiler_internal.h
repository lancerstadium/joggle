#pragma once

#include <optional>
#include <span>

#include "joggle/compiler.h"

namespace joggle::detail {

struct CompilerAccess {
  static std::optional<Type> make(Compiler& compiler,
                                  const Module::TypeDecl& schema,
                                  std::span<const ParameterValue> parameters) {
    return compiler.make(schema, parameters);
  }

  static std::optional<Attribute>
  make(Compiler& compiler, const Module::AttributeDecl& schema,
       std::span<const ParameterValue> parameters) {
    return compiler.make(schema, parameters);
  }
};

}  // namespace joggle::detail
