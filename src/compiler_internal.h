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

  static std::optional<ParameterValue>
  evaluate(Compiler& compiler, Module::FunctionDecl function,
           std::span<const ParameterValue> arguments,
           bool under_residual_control) {
    return compiler.evaluate_binding(std::move(function), arguments,
                                     under_residual_control);
  }

  static bool can_evaluate(const Compiler& compiler,
                           const Module::FunctionDecl& function,
                           bool under_residual_control) {
    return compiler.can_evaluate_binding(function, under_residual_control);
  }

  static bool accepts(Compiler& compiler, const Module::FunctionDecl& function,
                      const Module::ParameterDecl& parameter,
                      std::string_view cpp_type) {
    return compiler.accepts_host_type(function, parameter, cpp_type);
  }

  static std::optional<ExecutionValues>
  execute(Compiler& compiler, Module::FunctionDecl function,
          std::vector<ExecutionValue> arguments,
          bool under_residual_control) {
    return compiler.execute(std::move(function), std::move(arguments),
                            under_residual_control);
  }
};

}  // namespace joggle::detail
