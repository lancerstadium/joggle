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

namespace joggle::ir {

namespace transform_detail {

using CallReplacement = std::pair<std::size_t, joggle::Module::Function>;

struct FunctionCallPlan {
  std::string name;
  std::vector<CallReplacement> replacements;
};

struct ModuleCallPlan {
  std::vector<FunctionCallPlan> functions;
  std::size_t changed = 0;
};

template <typename Rule>
std::optional<std::size_t> rewrite_function(Function& function, Rule& rule,
                                            Diagnostics& diagnostics) {
  const std::size_t before = diagnostics.size();
  try {
    const auto instructions = function.instructions();
    auto edit = function.edit();
    std::size_t changed = 0;
    for (const Instruction& instruction : instructions) {
      if (!instruction.valid()) {
        continue;
      }
      if (std::invoke(rule, instruction, edit, diagnostics)) {
        ++changed;
      }
      if (diagnostics.size() != before) {
        return std::nullopt;
      }
    }
    if (changed == 0U) {
      return 0U;
    }
    return edit.commit(diagnostics)
               ? std::optional<std::size_t>{changed}
               : std::nullopt;
  } catch (const std::exception& error) {
    diagnostics.report("rewrite failed: " + std::string(error.what()));
  } catch (...) {
    diagnostics.report("rewrite failed with an unknown exception");
  }
  return std::nullopt;
}

template <typename Mapper>
std::optional<std::vector<CallReplacement>>
plan_calls(const Function& function, Mapper& mapper, Diagnostics& diagnostics) {
  std::vector<CallReplacement> replacements;
  try {
    const auto instructions = function.instructions();
    for (std::size_t index = 0; index < instructions.size(); ++index) {
      const Instruction& instruction = instructions[index];
      std::optional<joggle::Module::Function> replacement =
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
std::optional<ModuleCallPlan> plan_calls(const Module& module, Mapper& mapper,
                                         Diagnostics& diagnostics) {
  ModuleCallPlan plan;
  for (const joggle::Module::Function& member : module.functions()) {
    const std::string name(member.name());
    const Function* function = member.body();
    if (function == nullptr) {
      continue;
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

inline bool apply_calls(Module& module, const ModuleCallPlan& plan,
                        Diagnostics& diagnostics) {
  for (const FunctionCallPlan& function_plan : plan.functions) {
    Function* function = module.body(function_plan.name);
    if (function == nullptr) {
      diagnostics.report("Module lost function '" + function_plan.name + "'");
      return false;
    }
    if (!apply_calls(*function, function_plan.replacements, diagnostics)) {
      return false;
    }
  }
  return true;
}

}  // namespace transform_detail

// Applies one lambda to the committed Instructions present at the start of a
// sweep. The lambda receives (Instruction, Function::Edit, Diagnostics) and
// returns true only when it changed the IR. All edits commit together; an
// exception, diagnostic, or failed verification restores the prior Function.
template <typename Rule>
std::optional<std::size_t> rewrite(Function& function, Rule&& rule,
                                   Diagnostics& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Instruction&,
                                       Function::Edit&, Diagnostics&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_function(function, rule, diagnostics);
}

// Applies the same rule to every materialized Function on a private Module
// value. The Module is published only if every Function rewrite succeeds.
template <typename Rule>
std::optional<std::size_t> rewrite(Module& module, Rule&& rule,
                                   Diagnostics& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Instruction&,
                                       Function::Edit&, Diagnostics&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");

  Module candidate = module;
  std::size_t changed = 0;
  for (const joggle::Module::Function& member : module.functions()) {
    const Function* source = member.body();
    if (source == nullptr) {
      continue;
    }
    Function rewritten = *source;
    auto count =
        transform_detail::rewrite_function(rewritten, rule, diagnostics);
    if (!count) {
      return std::nullopt;
    }
    if (*count != 0U) {
      Function* target = candidate.body(member.name());
      if (target == nullptr) {
        diagnostics.report("Module lost function '" +
                           std::string(member.name()) + "'");
        return std::nullopt;
      }
      *target = std::move(rewritten);
    }
    changed += *count;
  }
  if (changed != 0U) {
    module = std::move(candidate);
  }
  return changed;
}

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
                            std::optional<joggle::Module::Function>>,
      "a call mapper must return "
      "std::optional<joggle::Module::Function>");

  auto replacements =
      transform_detail::plan_calls(function, mapper, diagnostics);
  if (!replacements ||
      !transform_detail::apply_calls(function, *replacements, diagnostics)) {
    return std::nullopt;
  }
  return replacements->size();
}

// Applies one call mapper to every Function on a private copy and publishes
// the new Module only if every Function verifies.
template <typename Mapper>
std::optional<std::size_t> map_calls(Module& module, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Instruction&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::Function>>,
      "a call mapper must return "
      "std::optional<joggle::Module::Function>");

  auto plan = transform_detail::plan_calls(static_cast<const Module&>(module),
                                           mapper, diagnostics);
  if (!plan) {
    return std::nullopt;
  }
  if (plan->functions.empty()) {
    return 0U;
  }

  Module candidate = module;
  if (!transform_detail::apply_calls(candidate, *plan, diagnostics)) {
    return std::nullopt;
  }
  module = std::move(candidate);
  return plan->changed;
}

inline std::optional<std::size_t>
replace_calls(Function& function, const joggle::Module::Function& from,
              const joggle::Module::Function& to,
              Diagnostics& diagnostics) {
  return map_calls(
      function,
      [&](const Instruction& instruction)
          -> std::optional<joggle::Module::Function> {
        return instruction.callee() == from
                   ? std::optional<joggle::Module::Function>{to}
                   : std::nullopt;
      },
      diagnostics);
}

inline std::optional<std::size_t>
replace_calls(Module& module, const joggle::Module::Function& from,
              const joggle::Module::Function& to,
              Diagnostics& diagnostics) {
  return map_calls(
      module,
      [&](const Instruction& instruction)
          -> std::optional<joggle::Module::Function> {
        return instruction.callee() == from
                   ? std::optional<joggle::Module::Function>{to}
                   : std::nullopt;
      },
      diagnostics);
}

}  // namespace joggle::ir
