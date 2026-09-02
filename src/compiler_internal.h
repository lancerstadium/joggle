#pragma once

#include <optional>
#include <span>

#include "joggle/compiler.h"

namespace joggle::detail {

struct CompilerAccess {
  static Compiler::EvaluationLimits limits(const Compiler& compiler) {
    return compiler.evaluation_limits();
  }

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

  static std::optional<ParameterValue>
  evaluate(Compiler& compiler, Module::FunctionDecl function,
           std::span<const ParameterValue> arguments) {
    return compiler.evaluate_binding(std::move(function), arguments);
  }
};

}  // namespace joggle::detail
