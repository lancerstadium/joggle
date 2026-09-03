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

struct FunctionCallPlan {
  std::string name;
  std::vector<CallReplacement> replacements;
};

struct ProgramCallPlan {
  std::vector<FunctionCallPlan> functions;
  std::size_t changed = 0;
};

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

template <typename Validate>
bool apply_calls(Function& function,
                 const std::vector<CallReplacement>& replacements,
                 Validate&& validate, Diagnostics& diagnostics) {
  if (replacements.empty()) {
    return std::invoke(validate, static_cast<const Function&>(function));
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
    if (!std::invoke(validate, static_cast<const Function&>(function))) {
      return false;
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

inline bool apply_calls(Function& function,
                        const std::vector<CallReplacement>& replacements,
                        Diagnostics& diagnostics) {
  return apply_calls(
      function, replacements, [](const Function&) { return true; },
      diagnostics);
}

template <typename Mapper>
std::optional<ProgramCallPlan> plan_calls(const Program& program,
                                          Mapper& mapper,
                                          Diagnostics& diagnostics) {
  ProgramCallPlan plan;
  for (const std::string& name : program.function_names()) {
    const Function* function = program.function(name);
    if (function == nullptr) {
      diagnostics.report("Program lost function '" + name + "'");
      return std::nullopt;
    }
    auto replacements = plan_calls(*function, mapper, diagnostics);
    if (!replacements) {
      return std::nullopt;
    }
    plan.changed += replacements->size();
    if (!replacements->empty()) {
      plan.functions.push_back({name, std::move(*replacements)});
    }
  }
  return plan;
}

inline bool apply_calls(Program& program, const ProgramCallPlan& plan,
                        Diagnostics& diagnostics) {
  for (const FunctionCallPlan& function_plan : plan.functions) {
    Function* function = program.function(function_plan.name);
    if (function == nullptr) {
      diagnostics.report("Program lost function '" + function_plan.name +
                         "'");
      return false;
    }
    if (!apply_calls(*function, function_plan.replacements, diagnostics)) {
      return false;
    }
  }
  return true;
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

  auto plan = transform_detail::plan_calls(
      static_cast<const Program&>(program), mapper, diagnostics);
  if (!plan) {
    return std::nullopt;
  }
  if (plan->functions.empty()) {
    return 0U;
  }

  Program candidate = program;
  if (!transform_detail::apply_calls(candidate, *plan, diagnostics)) {
    return std::nullopt;
  }
  program = std::move(candidate);
  return plan->changed;
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
