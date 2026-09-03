#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"
#include "joggle/module.h"
#include "joggle/program.h"

namespace joggle::ir {

namespace transform_detail {

using CallReplacement = std::pair<std::size_t, joggle::Module::FunctionDecl>;

template <typename Mapper>
std::optional<std::vector<CallReplacement>>
plan_calls(const Function& function, Mapper& mapper, Diagnostics& diagnostics) {
  std::vector<CallReplacement> replacements;
  try {
    const auto instructions = function.instructions();
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      const Instruction& instruction = instructions[index];
      std::optional<joggle::Module::FunctionDecl> replacement =
          std::invoke(mapper, instruction);
      if (replacement && *replacement != instruction.callee()) {
        replacements.emplace_back(index, std::move(*replacement));
      }
    }
  } catch (const std::exception& error) {
    diagnostics.report("call mapper failed: " + std::string(error.what()));
    return std::nullopt;
  } catch (...) {
    diagnostics.report("call mapper failed with an unknown exception");
    return std::nullopt;
  }
  return replacements;
}

inline bool apply_calls(Function& function,
                        const std::vector<CallReplacement>& replacements,
                        Diagnostics& diagnostics) {
  if (replacements.empty()) {
    return true;
  }
  try {
    const auto instructions = function.instructions();
    auto edit = function.edit();
    for (const auto& [index, replacement] : replacements) {
      if (index >= instructions.size()) {
        diagnostics.report("call mapping refers to a missing Instruction");
        return false;
      }
      edit.replace(instructions[index], replacement);
    }
    return edit.commit(diagnostics);
  } catch (const std::exception& error) {
    diagnostics.report("call mapping failed: " + std::string(error.what()));
    return false;
  } catch (...) {
    diagnostics.report("call mapping failed with an unknown exception");
    return false;
  }
}

}  // namespace transform_detail

// Transactionally maps call declarations in one Function. The mapper receives
// each committed Instruction and returns either a replacement declaration or
// std::nullopt to keep the call. The count is absent on failure; zero is a
// successful no-op.
template <typename Mapper>
std::optional<std::size_t> map_calls(Function& function, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Instruction&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::FunctionDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Module::FunctionDecl>");

  auto replacements =
      transform_detail::plan_calls(function, mapper, diagnostics);
  if (!replacements ||
      !transform_detail::apply_calls(function, *replacements, diagnostics)) {
    return std::nullopt;
  }
  return replacements->size();
}

// Applies one call mapper to every Function on a private copy and publishes
// the new Program only if every Function verifies.
template <typename Mapper>
std::optional<std::size_t> map_calls(Program& program, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Instruction&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::FunctionDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Module::FunctionDecl>");

  struct FunctionPlan {
    std::string name;
    std::vector<transform_detail::CallReplacement> replacements;
  };
  std::vector<FunctionPlan> plans;
  std::size_t changed = 0;
  const Program& source = program;
  for (const std::string& name : source.function_names()) {
    const Function* function = source.function(name);
    if (function == nullptr) {
      diagnostics.report("Program lost function '" + name + "'");
      return std::nullopt;
    }
    auto replacements =
        transform_detail::plan_calls(*function, mapper, diagnostics);
    if (!replacements) {
      return std::nullopt;
    }
    changed += replacements->size();
    if (!replacements->empty()) {
      plans.push_back({name, std::move(*replacements)});
    }
  }
  if (plans.empty()) {
    return 0U;
  }

  Program candidate = program;
  for (const FunctionPlan& plan : plans) {
    Function* function = candidate.function(plan.name);
    if (function == nullptr) {
      diagnostics.report("Program lost function '" + plan.name + "'");
      return std::nullopt;
    }
    if (!transform_detail::apply_calls(*function, plan.replacements,
                                       diagnostics)) {
      return std::nullopt;
    }
  }
  program = std::move(candidate);
  return changed;
}

inline std::optional<std::size_t>
replace_calls(Function& function, const joggle::Module::FunctionDecl& from,
              const joggle::Module::FunctionDecl& to,
              Diagnostics& diagnostics) {
  return map_calls(
      function,
      [&](const Instruction& instruction)
          -> std::optional<joggle::Module::FunctionDecl> {
        return instruction.callee() == from
                   ? std::optional<joggle::Module::FunctionDecl>{to}
                   : std::nullopt;
      },
      diagnostics);
}

inline std::optional<std::size_t>
replace_calls(Program& program, const joggle::Module::FunctionDecl& from,
              const joggle::Module::FunctionDecl& to,
              Diagnostics& diagnostics) {
  return map_calls(
      program,
      [&](const Instruction& instruction)
          -> std::optional<joggle::Module::FunctionDecl> {
        return instruction.callee() == from
                   ? std::optional<joggle::Module::FunctionDecl>{to}
                   : std::nullopt;
      },
      diagnostics);
}

}  // namespace joggle::ir
