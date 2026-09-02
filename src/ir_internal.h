#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "joggle/ir.h"

namespace joggle::detail {

struct FunctionIdentity;

// Internal access shared by the parser and compiler. Source provenance is
// deliberately not part of the public function-editing surface.
struct FunctionAccess {
  static const std::shared_ptr<FunctionIdentity>& owner(const Value& value);
  static const std::shared_ptr<FunctionIdentity>&
  owner(const Instruction& instruction);
  static const std::shared_ptr<FunctionIdentity>& owner(const Block& block);

  static std::uint64_t id(const Value& value);
  static std::uint64_t id(const Instruction& instruction);
  static std::uint64_t id(const Block& block);

  static void locate(Function::Edit& edit, const Instruction& instruction,
                     SourceRange source);
  static std::optional<SourceRange> location(const Instruction& instruction);
  static std::optional<ParameterValue> property(const Instruction& instruction,
                                                std::string_view name);
  static bool verify_structure(const Function& function, Diagnostics& diagnostics);
  static bool verify_contracts(const Function& function, Diagnostics& diagnostics);
  static bool verify_contracts(const Function& function, Compiler& compiler,
                               Diagnostics& diagnostics);
  static void declare(Function& function, Module::FunctionDecl declaration,
                      std::vector<Type> argument_types,
                      std::vector<Type> result_types);
  static bool commit(Function::Edit& edit, Compiler& compiler,
                     Diagnostics& diagnostics);
};

struct PropertyAccess {
  static std::string take_name(Property& property);
  static ParameterValue take_value(Property& property);
};

}  // namespace joggle::detail
