#pragma once

#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"
#include "joggle/module.h"

namespace joggle {

// Clones an arbitrary CFG while mapping every IR type. The optional callee
// mapper supports one-to-one vocabulary conversion during the same verified
// construction. Known properties are preserved; the returned Function is a
// standalone value and can be inserted into a destination Module.
std::optional<Function> clone(
    Compiler& compiler, const Function& source,
    const std::function<std::optional<Type>(const Value&)>& map_value_type,
    const std::function<std::optional<Module::FunctionDecl>(const Op&)>&
        map_callee,
    Diagnostics& diagnostics);

std::optional<Function> clone(
    Compiler& compiler, const Function& source,
    const std::function<std::optional<Type>(const Value&)>& map_value_type,
    Diagnostics& diagnostics);

// Structurally replaces every maximal non-overlapping occurrence of before
// with after in one transaction. This low-level overload checks types, data
// flow, and effects but not semantic equivalence. Compiler-facing modules
// should normally use the Compiler& overload below. Zero is a successful
// no-op.
std::optional<std::size_t> replace(Function& function, const Function& before,
                                   const Function& after,
                                   Diagnostics& diagnostics);

// Applies the same replacement to every materialized member on a private
// Module value and publishes only when all member transactions succeed.
std::optional<std::size_t> replace(Module& module, const Function& before,
                                   const Function& after,
                                   Diagnostics& diagnostics);

// Proves conservative definitional equivalence by recursively expanding
// eligible source-bodied calls in two pure expression Functions. Opaque calls
// remain exact-identity leaves. No secondary normalization IR is introduced,
// and the input Functions are not mutated.
bool equivalent(Compiler& compiler, const Function& left,
                const Function& right, Diagnostics& diagnostics,
                std::size_t max_expansions = 256U);

// Proves the same relation after mapping every observed value Type through an
// idempotent logical projection. This is intended for representation-changing
// modules: the module owns the projection, while source bodies still account
// for every changed call. A missing or non-idempotent projection fails closed.
using TypeProjection =
    std::function<std::optional<Type>(const Type&)>;
bool equivalent(Compiler& compiler, const Function& left,
                const Function& right, const TypeProjection& project,
                Diagnostics& diagnostics,
                std::size_t max_expansions = 256U);

// Checks definitional equivalence before performing the existing atomic typed
// replacement. A failed proof publishes no edit.
std::optional<std::size_t>
replace(Compiler& compiler, Function& function, const Function& before,
        const Function& after, Diagnostics& diagnostics,
        std::size_t max_expansions = 256U);

std::optional<std::size_t>
replace(Compiler& compiler, Module& module, const Function& before,
        const Function& after, Diagnostics& diagnostics,
        std::size_t max_expansions = 256U);

namespace transform_detail {

template <typename Rule>
std::optional<std::size_t> rewrite_function(Function& function, Rule& rule,
                                            Diagnostics& diagnostics) {
  const std::size_t before = diagnostics.size();
  try {
    const auto ops = function.ops();
    auto edit = function.edit();
    std::size_t changed = 0;
    for (const Op& op : ops) {
      if (!op.valid()) {
        continue;
      }
      if (std::invoke(rule, op, edit, diagnostics)) {
        ++changed;
      }
      if (diagnostics.size() != before) {
        return std::nullopt;
      }
    }
    if (changed == 0U) {
      return 0U;
    }
    return edit.commit(diagnostics) ? std::optional<std::size_t>{changed}
                                    : std::nullopt;
  } catch (const std::exception& error) {
    diagnostics.report("rewrite failed: " + std::string(error.what()));
  } catch (...) {
    diagnostics.report("rewrite failed with an unknown exception");
  }
  return std::nullopt;
}

}  // namespace transform_detail

// Applies one lambda to the committed Ops present at the start of a
// sweep. The lambda receives (Op, Function::Edit, Diagnostics) and
// returns true only when it changed the IR. All edits commit together; an
// exception, diagnostic, or failed verification restores the prior Function.
template <typename Rule>
std::optional<std::size_t> rewrite(Function& function, Rule&& rule,
                                   Diagnostics& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&,
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
  using Changed = std::invoke_result_t<Rule&, const Op&,
                                       Function::Edit&, Diagnostics&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");

  Module candidate = module;
  std::size_t changed = 0;
  for (const joggle::Module::FunctionDecl& member : module.functions()) {
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
      Function* target = candidate.body(member);
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

template <typename Subject, typename Rule>
std::optional<std::size_t> rewrite_to_fixpoint(Subject& subject, Rule& rule,
                                               std::size_t max_iterations,
                                               Diagnostics& diagnostics) {
  if (max_iterations == 0U) {
    diagnostics.report("a fixed-point rewrite needs at least one iteration");
    return std::nullopt;
  }
  Subject candidate = subject;
  std::size_t total = 0;
  for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
    auto changed = rewrite(candidate, rule, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    if (*changed == 0U) {
      if (total != 0U) {
        subject = std::move(candidate);
      }
      return total;
    }
    if (std::numeric_limits<std::size_t>::max() - total < *changed) {
      diagnostics.report("fixed-point rewrite change count overflowed");
      return std::nullopt;
    }
    total += *changed;
  }
  diagnostics.report("rewrite did not converge after " +
                     std::to_string(max_iterations) + " iterations");
  return std::nullopt;
}

}  // namespace transform_detail

// Repeats transactional sweeps until one makes no changes. All intermediate
// sweeps stay private; exhausting the explicit limit publishes nothing.
template <typename Rule>
std::optional<std::size_t> rewrite_to_fixpoint(Function& function, Rule&& rule,
                                               std::size_t max_iterations,
                                               Diagnostics& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&,
                                       Function::Edit&, Diagnostics&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_to_fixpoint(function, rule, max_iterations,
                                               diagnostics);
}

template <typename Rule>
std::optional<std::size_t> rewrite_to_fixpoint(Module& module, Rule&& rule,
                                               std::size_t max_iterations,
                                               Diagnostics& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&,
                                       Function::Edit&, Diagnostics&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_to_fixpoint(module, rule, max_iterations,
                                               diagnostics);
}

namespace transform_detail {

template <typename Legal>
bool legal(const Function& function, Legal& predicate, Diagnostics& diagnostics,
           std::string_view member = {}) {
  try {
    for (const Op& op : function.ops()) {
      if (std::invoke(predicate, op)) {
        continue;
      }
      std::string message = "conversion left illegal call '" +
                            op.callee().symbol().qualified_name() +
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
  for (const joggle::Module::FunctionDecl& member : module.functions()) {
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
// Op satisfies the caller's legality predicate.
template <typename Rule, typename Legal>
std::optional<std::size_t> convert(Function& function, Rule&& rule,
                                   Legal&& legal, Diagnostics& diagnostics) {
  using Accepted = std::invoke_result_t<Legal&, const Op&>;
  static_assert(std::is_convertible_v<Accepted, bool>,
                "a conversion legality predicate must return bool");

  Function candidate = function;
  auto changed = rewrite(candidate, rule, diagnostics);
  if (!changed || !transform_detail::legal(candidate, legal, diagnostics)) {
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
  using Accepted = std::invoke_result_t<Legal&, const Op&>;
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
// each committed Op and returns either a replacement declaration or
// std::nullopt to keep the call. The count is absent on failure; zero is a
// successful no-op.
template <typename Mapper>
std::optional<std::size_t> map_calls(Function& function, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Op&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::FunctionDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Module::FunctionDecl>");

  return rewrite(
      function,
      [&](const Op& op, Function::Edit& edit, Diagnostics&) {
        std::optional<joggle::Module::FunctionDecl> replacement =
            std::invoke(mapper, op);
        if (!replacement || *replacement == op.callee()) {
          return false;
        }
        edit.replace(op, *replacement);
        return true;
      },
      diagnostics);
}

// Applies the same convenience mapping through the Module rewrite transaction.
template <typename Mapper>
std::optional<std::size_t> map_calls(Module& module, Mapper&& mapper,
                                     Diagnostics& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Op&>;
  static_assert(
      std::is_convertible_v<Mapped,
                            std::optional<joggle::Module::FunctionDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Module::FunctionDecl>");

  return rewrite(
      module,
      [&](const Op& op, Function::Edit& edit, Diagnostics&) {
        std::optional<joggle::Module::FunctionDecl> replacement =
            std::invoke(mapper, op);
        if (!replacement || *replacement == op.callee()) {
          return false;
        }
        edit.replace(op, *replacement);
        return true;
      },
      diagnostics);
}

inline std::optional<std::size_t>
replace_calls(Function& function, const joggle::Module::FunctionDecl& from,
              const joggle::Module::FunctionDecl& to,
              Diagnostics& diagnostics) {
  return map_calls(
      function,
      [&](const Op& op)
          -> std::optional<joggle::Module::FunctionDecl> {
        return op.callee() == from
                   ? std::optional<joggle::Module::FunctionDecl>{to}
                   : std::nullopt;
      },
      diagnostics);
}

inline std::optional<std::size_t>
replace_calls(Module& module, const joggle::Module::FunctionDecl& from,
              const joggle::Module::FunctionDecl& to,
              Diagnostics& diagnostics) {
  return map_calls(
      module,
      [&](const Op& op)
          -> std::optional<joggle::Module::FunctionDecl> {
        return op.callee() == from
                   ? std::optional<joggle::Module::FunctionDecl>{to}
                   : std::nullopt;
      },
      diagnostics);
}

}  // namespace joggle
