#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "joggle/ir.h"

namespace joggle::detail {

using ir::Block;
using ir::Function;
using ir::Instruction;
using ir::Terminator;
using ir::Value;

struct FunctionIdentity;
struct FunctionState;

// Internal access shared by the parser and compiler. Source provenance is
// deliberately not part of the public function-editing surface.
struct FunctionAccess {
  static const std::shared_ptr<FunctionIdentity>& owner(const Value& value);
  static const std::shared_ptr<FunctionIdentity>&
  owner(const Instruction& instruction);
  static const std::shared_ptr<FunctionIdentity>& owner(const Block& block);
  static const std::shared_ptr<const KnownValueStorage>&
  known(const Value& value);

  static std::uint64_t id(const Value& value);
  static std::uint64_t id(const Instruction& instruction);
  static std::uint64_t id(const Block& block);
  static Value restore(std::shared_ptr<FunctionIdentity> function,
                       std::uint64_t id,
                       std::shared_ptr<const KnownValueStorage> known);

  static void locate(Function::Edit& edit, const Instruction& instruction,
                     SourceRange source);
  static std::optional<SourceRange> location(const Instruction& instruction);
  static std::optional<ParameterValue> known_value(const Value& value);
  static std::size_t argument_parameter(const Instruction& instruction,
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
  static bool commit(Function::Edit& edit, Compiler& compiler,
                     Diagnostics& diagnostics);
};

}  // namespace joggle::detail
