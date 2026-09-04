#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "compile/native.h"
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

struct joggle::Compiler::State {
  struct LockedIdentity {
    Version version;
    std::string digest;
    bool root = false;
  };
  struct LockedNative {
    Version mod_version;
    std::string mod_digest;
    std::string target;
    std::string digest;
  };
  struct HostRepresentation {
    Mod::TypeDecl schema;
    RepresentationProjector project;
  };
  struct BoundFn {
    NativeFn callable;
    HostEval evaluation = HostEval::Guarded;
  };

  Diag diagnostics;
  std::map<std::string, Mod, std::less<>> mods;
  std::map<std::string, std::filesystem::path, std::less<>> mod_sources;
  std::set<std::string, std::less<>> explicit_mods;
  std::vector<std::filesystem::path> search_paths;
  std::map<std::string, LockedIdentity, std::less<>> locked_mods;
  std::map<std::string, LockedNative, std::less<>> locked_natives;
  bool has_lock = false;
  std::vector<joggle::detail::Library> native_libraries;
  std::set<std::string, std::less<>> loaded_natives;
  std::map<std::string, VerifierFn<Type>, std::less<>> type_verifiers;
  std::map<std::string, VerifierFn<Op>, std::less<>> op_verifiers;
  std::map<std::string, BoundFn, std::less<>> bindings;
  std::map<std::string, joggle::detail::ParamVal, std::less<>>
      hermetic_evaluations;
  std::map<std::string, HostRepresentation, std::less<>> host_types;
  std::map<std::string, std::string, std::less<>> host_representations;
  std::set<std::string, std::less<>> constructing_types;
  Limits evaluation_limits;
  bool linked = false;
};
