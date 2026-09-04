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

#include "joggle/diag.h"
#include "joggle/ir.h"
#include "joggle/mod.h"

namespace joggle {

// Clones an arbitrary CFG while mapping every IR type. The optional callee
// mapper supports one-to-one vocabulary conversion during the same verified
// construction. Known properties are preserved; the returned Fn is a
// standalone value and can be inserted into a destination Mod.
std::optional<Fn>
clone(Compiler& compiler, const Fn& source,
      const std::function<std::optional<Type>(const Val&)>& map_value_type,
      const std::function<std::optional<Mod::FnDecl>(const Op&)>& map_callee,
      Diag& diagnostics);

std::optional<Fn>
clone(Compiler& compiler, const Fn& source,
      const std::function<std::optional<Type>(const Val&)>& map_value_type,
      Diag& diagnostics);

// Structurally replaces every maximal non-overlapping occurrence of before
// with after in one transaction. This low-level overload checks types, data
// flow, and effects but not semantic equivalence. Compiler-facing mods
// should normally use the Compiler& overload below. Zero is a successful
// no-op.
std::optional<std::size_t> replace(Fn& fn, const Fn& before, const Fn& after,
                                   Diag& diagnostics);

// Applies the same replacement to every materialized member on a private
// Mod value and publishes only when all member transactions succeed.
std::optional<std::size_t> replace(Mod& mod, const Fn& before, const Fn& after,
                                   Diag& diagnostics);

// Proves conservative definitional equivalence by recursively expanding
// eligible source-bodied calls in two pure expression Fns. Opaque calls
// remain exact-identity leaves. No secondary normalization IR is introduced,
// and the input Fns are not mutated.
bool equivalent(Compiler& compiler, const Fn& left, const Fn& right,
                Diag& diagnostics, std::size_t max_expansions = 256U);

// Checks definitional equivalence before performing the existing atomic typed
// replacement. A failed proof publishes no edit.
std::optional<std::size_t> replace(Compiler& compiler, Fn& fn, const Fn& before,
                                   const Fn& after, Diag& diagnostics,
                                   std::size_t max_expansions = 256U);

std::optional<std::size_t> replace(Compiler& compiler, Mod& mod,
                                   const Fn& before, const Fn& after,
                                   Diag& diagnostics,
                                   std::size_t max_expansions = 256U);

namespace transform_detail {

template <typename Rule>
std::optional<std::size_t> rewrite_fn(Fn& fn, Rule& rule, Diag& diagnostics) {
  const std::size_t before = diagnostics.size();
  try {
    const auto ops = fn.ops();
    auto edit = fn.edit();
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
// sweep. The lambda receives (Op, Fn::Edit, Diag) and
// returns true only when it changed the IR. All edits commit together; an
// exception, diagnostic, or failed verification restores the prior Fn.
template <typename Rule>
std::optional<std::size_t> rewrite(Fn& fn, Rule&& rule, Diag& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&, Fn::Edit&, Diag&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_fn(fn, rule, diagnostics);
}

// Applies the same rule to every materialized Fn on a private Mod
// value. The Mod is published only if every Fn rewrite succeeds.
template <typename Rule>
std::optional<std::size_t> rewrite(Mod& mod, Rule&& rule, Diag& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&, Fn::Edit&, Diag&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");

  Mod candidate = mod;
  std::size_t changed = 0;
  for (const joggle::Mod::FnDecl& member : mod.fns()) {
    const Fn* source = member.body();
    if (source == nullptr) {
      continue;
    }
    Fn rewritten = *source;
    auto count = transform_detail::rewrite_fn(rewritten, rule, diagnostics);
    if (!count) {
      return std::nullopt;
    }
    if (*count != 0U) {
      Fn* target = candidate.body(member);
      if (target == nullptr) {
        diagnostics.report("Mod lost fn '" + std::string(member.name()) + "'");
        return std::nullopt;
      }
      *target = std::move(rewritten);
    }
    changed += *count;
  }
  if (changed != 0U) {
    mod = std::move(candidate);
  }
  return changed;
}

namespace transform_detail {

template <typename Subject, typename Rule>
std::optional<std::size_t> rewrite_to_fixpoint(Subject& subject, Rule& rule,
                                               std::size_t max_iterations,
                                               Diag& diagnostics) {
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
std::optional<std::size_t> rewrite_to_fixpoint(Fn& fn, Rule&& rule,
                                               std::size_t max_iterations,
                                               Diag& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&, Fn::Edit&, Diag&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_to_fixpoint(fn, rule, max_iterations,
                                               diagnostics);
}

template <typename Rule>
std::optional<std::size_t> rewrite_to_fixpoint(Mod& mod, Rule&& rule,
                                               std::size_t max_iterations,
                                               Diag& diagnostics) {
  using Changed = std::invoke_result_t<Rule&, const Op&, Fn::Edit&, Diag&>;
  static_assert(std::is_convertible_v<Changed, bool>,
                "a rewrite lambda must return bool");
  return transform_detail::rewrite_to_fixpoint(mod, rule, max_iterations,
                                               diagnostics);
}

namespace transform_detail {

template <typename Legal>
bool legal(const Fn& fn, Legal& predicate, Diag& diagnostics,
           std::string_view member = {}) {
  try {
    for (const Op& op : fn.ops()) {
      if (std::invoke(predicate, op)) {
        continue;
      }
      std::string message = "conversion left illegal call '" +
                            op.callee().symbol().qualified_name() + "'";
      if (!member.empty()) {
        message += " in fn '" + std::string(member) + "'";
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
bool legal(const Mod& mod, Legal& predicate, Diag& diagnostics) {
  for (const joggle::Mod::FnDecl& member : mod.fns()) {
    const Fn* fn = member.body();
    if (fn != nullptr && !legal(*fn, predicate, diagnostics, member.name())) {
      return false;
    }
  }
  return true;
}

}  // namespace transform_detail

// Rewrites a private Fn value and publishes it only if every remaining
// Op satisfies the caller's legality predicate.
template <typename Rule, typename Legal>
std::optional<std::size_t> convert(Fn& fn, Rule&& rule, Legal&& legal,
                                   Diag& diagnostics) {
  using Accepted = std::invoke_result_t<Legal&, const Op&>;
  static_assert(std::is_convertible_v<Accepted, bool>,
                "a conversion legality predicate must return bool");

  Fn candidate = fn;
  auto changed = rewrite(candidate, rule, diagnostics);
  if (!changed || !transform_detail::legal(candidate, legal, diagnostics)) {
    return std::nullopt;
  }
  if (*changed != 0U) {
    fn = std::move(candidate);
  }
  return changed;
}

// Applies the same contract to all materialized Fns. Both rewriting and
// the final legality check are atomic at the Mod boundary.
template <typename Rule, typename Legal>
std::optional<std::size_t> convert(Mod& mod, Rule&& rule, Legal&& legal,
                                   Diag& diagnostics) {
  using Accepted = std::invoke_result_t<Legal&, const Op&>;
  static_assert(std::is_convertible_v<Accepted, bool>,
                "a conversion legality predicate must return bool");

  Mod candidate = mod;
  auto changed = rewrite(candidate, rule, diagnostics);
  if (!changed || !transform_detail::legal(candidate, legal, diagnostics)) {
    return std::nullopt;
  }
  if (*changed != 0U) {
    mod = std::move(candidate);
  }
  return changed;
}

// Transactionally maps call declarations in one Fn. The mapper receives
// each committed Op and returns either a replacement declaration or
// std::nullopt to keep the call. The count is absent on failure; zero is a
// successful no-op.
template <typename Mapper>
std::optional<std::size_t> map_calls(Fn& fn, Mapper&& mapper,
                                     Diag& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Op&>;
  static_assert(
      std::is_convertible_v<Mapped, std::optional<joggle::Mod::FnDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Mod::FnDecl>");

  return rewrite(
      fn,
      [&](const Op& op, Fn::Edit& edit, Diag&) {
        std::optional<joggle::Mod::FnDecl> replacement =
            std::invoke(mapper, op);
        if (!replacement || *replacement == op.callee()) {
          return false;
        }
        edit.replace(op, *replacement);
        return true;
      },
      diagnostics);
}

// Applies the same convenience mapping through the Mod rewrite transaction.
template <typename Mapper>
std::optional<std::size_t> map_calls(Mod& mod, Mapper&& mapper,
                                     Diag& diagnostics) {
  using Mapped = std::invoke_result_t<Mapper&, const Op&>;
  static_assert(
      std::is_convertible_v<Mapped, std::optional<joggle::Mod::FnDecl>>,
      "a call mapper must return "
      "std::optional<joggle::Mod::FnDecl>");

  return rewrite(
      mod,
      [&](const Op& op, Fn::Edit& edit, Diag&) {
        std::optional<joggle::Mod::FnDecl> replacement =
            std::invoke(mapper, op);
        if (!replacement || *replacement == op.callee()) {
          return false;
        }
        edit.replace(op, *replacement);
        return true;
      },
      diagnostics);
}

inline std::optional<std::size_t> replace_calls(Fn& fn,
                                                const joggle::Mod::FnDecl& from,
                                                const joggle::Mod::FnDecl& to,
                                                Diag& diagnostics) {
  return map_calls(
      fn,
      [&](const Op& op) -> std::optional<joggle::Mod::FnDecl> {
        return op.callee() == from ? std::optional<joggle::Mod::FnDecl>{to}
                                   : std::nullopt;
      },
      diagnostics);
}

inline std::optional<std::size_t> replace_calls(Mod& mod,
                                                const joggle::Mod::FnDecl& from,
                                                const joggle::Mod::FnDecl& to,
                                                Diag& diagnostics) {
  return map_calls(
      mod,
      [&](const Op& op) -> std::optional<joggle::Mod::FnDecl> {
        return op.callee() == from ? std::optional<joggle::Mod::FnDecl>{to}
                                   : std::nullopt;
      },
      diagnostics);
}

}  // namespace joggle
