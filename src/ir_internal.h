#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "joggle/ir.h"

namespace joggle::detail {

struct FunctionIdentity;
struct FunctionState;

// Internal access shared by the parser and compiler. Source provenance is
// deliberately not part of the public function-editing surface.
struct FunctionAccess {
  static const std::shared_ptr<FunctionIdentity>& owner(const Value& value);
  static const std::shared_ptr<FunctionIdentity>&
  owner(const Op& op);
  static const std::shared_ptr<FunctionIdentity>& owner(const Block& block);
  static const std::shared_ptr<const KnownValueStorage>&
  known(const Value& value);

  static std::uint64_t id(const Value& value);
  static std::uint64_t id(const Op& op);
  static std::uint64_t id(const Block& block);
  static Value restore(std::shared_ptr<FunctionIdentity> function,
                       std::uint64_t id,
                       std::shared_ptr<const KnownValueStorage> known);

  static void locate(Function::Edit& edit, const Op& op,
                     SourceRange source);
  static std::optional<SourceRange> location(const Op& op);
  static std::optional<ParameterValue> known_value(const Value& value);
  static std::size_t argument_parameter(const Op& op,
                                        std::size_t argument);
  static bool verify_structure(const Function& function,
                               Diagnostics& diagnostics);
  static bool verify_contracts(const Function& function,
                               Diagnostics& diagnostics);
  static bool verify_contracts(const Function& function, Compiler& compiler,
                               Diagnostics& diagnostics);
  static void declare(Function& function, Module::FunctionDecl declaration,
                      std::vector<Type> argument_types,
                      std::vector<Type> result_types);
  static void define(Function& function, std::vector<Type> argument_types,
                     std::vector<Type> result_types);
  static bool attach(Function& function, Module::FunctionDecl declaration,
                     Module owner, Diagnostics& diagnostics);
  static bool commit(Function::Edit& edit, Compiler& compiler,
                     Diagnostics& diagnostics);
};

}  // namespace joggle::detail
