#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"
#include "joggle/module.h"

namespace joggle::ir {

namespace transform_detail {

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

namespace transform_detail {

template <typename Legal>
bool legal(const Function& function, Legal& predicate,
           Diagnostics& diagnostics, std::string_view member = {}) {
  try {
    for (const Instruction& instruction : function.instructions()) {
      if (std::invoke(predicate, instruction)) {
        continue;
      }
      std::string message = "conversion left illegal call '" +
                            instruction.callee().symbol().qualified_name() +
                            "'";
      if (!member.empty()) {
        message += " in function '" + std::string(member) + "'";
      }
      diagnostics.report(std::move(message));
      return false;
    }
  } catch (const std::exception& error) {
    diagnostics.report("conversion legality check failed: " +
                       std::string(error.what()));
    return false;
  } catch (...) {
    diagnostics.report("conversion legality check failed with an unknown "
                       "exception");
    return false;
  }
  return true;
}

template <typename Legal>
bool legal(const Module& module, Legal& predicate, Diagnostics& diagnostics) {
  for (const joggle::Module::Function& member : module.functions()) {
    const Function* function = member.body();
    if (function != nullptr &&
        !legal(*function, predicate, diagnostics, member.name())) {
      return false;
    }
  }
  return true;
}

}  // namespace transform_detail

// Rewrites a private Function value and publishes it only if every remaining
// Instruction satisfies the caller's legality predicate.
template <typename Rule, typename Legal>
std::optional<std::size_t> convert(Function& function, Rule&& rule,
                                   Legal&& legal, Diagnostics& diagnostics) {
  using Accepted = std::invoke_result_t<Legal&, const Instruction&>;
  static_assert(std::is_convertible_v<Accepted, bool>,
                "a conversion legality predicate must return bool");

  Function candidate = function;
  auto changed = rewrite(candidate, rule, diagnostics);
  if (!changed ||
      !transform_detail::legal(candidate, legal, diagnostics)) {
    return std::nullopt;
  }
  if (*changed != 0U) {
    function = std::move(candidate);
  }
  return changed;
}

// Applies the same contract to all materialized Functions. Both rewriting and
// the final legality check are atomic at the Module boundary.
template <typename Rule, typename Legal>
std::optional<std::size_t> convert(Module& module, Rule&& rule, Legal&& legal,
                                   Diagnostics& diagnostics) {
  using Accepted = std::invoke_result_t<Legal&, const Instruction&>;
  static_assert(std::is_convertible_v<Accepted, bool>,
                "a conversion legality predicate must return bool");

  Module candidate = module;
  auto changed = rewrite(candidate, rule, diagnostics);
  if (!changed || !transform_detail::legal(candidate, legal, diagnostics)) {
    return std::nullopt;
  }
  if (*changed != 0U) {
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

  return rewrite(
      function,
      [&](const Instruction& instruction, Function::Edit& edit,
          Diagnostics&) {
        std::optional<joggle::Module::Function> replacement =
            std::invoke(mapper, instruction);
        if (!replacement || *replacement == instruction.callee()) {
          return false;
        }
        edit.replace(instruction, *replacement);
        return true;
      },
      diagnostics);
}

// Applies the same convenience mapping through the Module rewrite transaction.
template <typename Mapper>
std::optional<std::size_t> map_calls(Module& module, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Instruction&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::Function>>,
      "a call mapper must return "
      "std::optional<joggle::Module::Function>");

  return rewrite(
      module,
      [&](const Instruction& instruction, Function::Edit& edit,
          Diagnostics&) {
        std::optional<joggle::Module::Function> replacement =
            std::invoke(mapper, instruction);
        if (!replacement || *replacement == instruction.callee()) {
          return false;
        }
        edit.replace(instruction, *replacement);
        return true;
      },
      diagnostics);
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
