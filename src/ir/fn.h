#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "joggle/ir.h"

namespace joggle::detail {

struct FnIdentity;
struct FnState;

// Internal access shared by the parser and compiler. Public edits expose the
// same source-location field without exposing Fn storage.
struct FnAccess {
  static const std::shared_ptr<FnIdentity>& owner(const Val& value);
  static const std::shared_ptr<FnIdentity>& owner(const Op& op);
  static const std::shared_ptr<FnIdentity>& owner(const Blk& block);
  static const std::shared_ptr<const KnownValStorage>& known(const Val& value);

  static std::uint64_t id(const Val& value);
  static std::uint64_t id(const Op& op);
  static std::uint64_t id(const Blk& block);
  static Val restore(std::shared_ptr<FnIdentity> fn, std::uint64_t id,
                     std::shared_ptr<const KnownValStorage> known);

  static void locate(Fn::Edit& edit, const Op& op, Loc source);
  static std::optional<Loc> location(const Op& op);
  static std::optional<ParamVal> known_value(const Val& value);
  static std::size_t argument_parameter(const Op& op, std::size_t argument);
  // Moves the operations and terminator after `before` into a continuation
  // block whose arguments have the Call result Types. The Call remains as the
  // last operation of its original block until its owner rewires and erases it.
  static Blk split(Fn::Edit& edit, Op before);
  static bool verify_structure(const Fn& fn, Diag& diagnostics);
  static bool verify_contracts(const Fn& fn, Diag& diagnostics);
  static bool verify_contracts(const Fn& fn, Compiler& compiler,
                               Diag& diagnostics);
  static void declare(Fn& fn, Mod::FnDecl declaration,
                      std::vector<Type> argument_types,
                      std::vector<Type> result_types);
  static void define(Fn& fn, std::vector<Type> argument_types,
                     std::vector<Type> result_types);
  static bool attach(Fn& fn, Mod::FnDecl declaration, Mod owner,
                     Diag& diagnostics);
  static bool commit(Fn::Edit& edit, Compiler& compiler, Diag& diagnostics);
};

}  // namespace joggle::detail
