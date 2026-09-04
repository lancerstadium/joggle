#pragma once

#include <optional>
#include <span>

#include "joggle/compiler.h"

namespace joggle::detail {

struct CompilerAccess {
  static Compiler::Limits limits(const Compiler& compiler) {
    return compiler.evaluation_limits();
  }

  static std::optional<Type> make(Compiler& compiler,
                                  const Mod::TypeDecl& schema,
                                  std::span<const ParamVal> parameters) {
    return compiler.make(schema, parameters);
  }

  static std::optional<ParamVal> evaluate(Compiler& compiler, Mod::FnDecl fn,
                                          std::span<const ParamVal> arguments,
                                          bool under_residual_control) {
    return compiler.evaluate_binding(std::move(fn), arguments,
                                     under_residual_control);
  }

  static bool can_evaluate(const Compiler& compiler, const Mod::FnDecl& fn,
                           bool under_residual_control) {
    return compiler.can_evaluate_binding(fn, under_residual_control);
  }

  static bool accepts(Compiler& compiler, const Mod::FnDecl& fn,
                      const Mod::ParamDecl& parameter,
                      std::string_view cpp_type) {
    return compiler.accepts_host_type(fn, parameter, cpp_type);
  }

  static std::optional<ExecVals> execute(Compiler& compiler, Mod::FnDecl fn,
                                         std::vector<ExecVal> arguments,
                                         bool under_residual_control) {
    return compiler.execute(std::move(fn), std::move(arguments),
                            under_residual_control);
  }
};

}  // namespace joggle::detail
