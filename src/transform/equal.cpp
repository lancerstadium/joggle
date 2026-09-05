#include "joggle/transform.h"

#include "ir/fn.h"
#include "transform/match.h"

#include "joggle/compiler.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace joggle {
namespace {

void field(std::string& output, std::string_view value) {
  output += std::to_string(value.size());
  output += ':';
  output += value;
}

std::optional<std::size_t> position(const std::vector<Val>& values,
                                    const Val& value) {
  const auto found = std::find(values.begin(), values.end(), value);
  return found == values.end()
             ? std::nullopt
             : std::optional<std::size_t>{
                   static_cast<std::size_t>(found - values.begin())};
}

class Normalizer {
public:
  Normalizer(Compiler& compiler, std::size_t max_expansions, Diag& diagnostics)
      : compiler_(compiler), max_expansions_(max_expansions),
        diagnostics_(diagnostics) {}

  std::optional<std::string> run(const Fn& fn, std::string_view role) {
    if (!detail::validate_expression_template(fn, role, diagnostics_)) {
      return std::nullopt;
    }
    std::vector<std::string> arguments;
    const auto inputs = fn.arguments();
    arguments.reserve(inputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      std::string argument = "argument";
      field(argument, std::to_string(index));
      field(argument, inputs[index].type().stable_name());
      arguments.push_back(std::move(argument));
    }
    return value(fn.entry().terminator().returned().front(), inputs, arguments);
  }

private:
  std::optional<std::string> value(const Val& current,
                                   const std::vector<Val>& parameters,
                                   const std::vector<std::string>& arguments) {
    const auto cached =
        std::find_if(memo_.begin(), memo_.end(),
                     [&](const auto& entry) { return entry.first == current; });
    if (cached != memo_.end()) {
      return cached->second;
    }
    const auto remember = [&](std::optional<std::string> result) {
      if (result) {
        memo_.emplace_back(current, *result);
      }
      return result;
    };
    if (const auto index = position(parameters, current)) {
      return remember(arguments[*index]);
    }
    if (current.known()) {
      const auto known = detail::FnAccess::known_value(current);
      if (!known) {
        diagnostics_.report("equivalence lost a Known value");
        return std::nullopt;
      }
      std::string result = "known";
      field(result, current.type().stable_name());
      field(result, known->canonical());
      return remember(std::move(result));
    }
    if (const auto reference = current.referenced_fn()) {
      std::string result = "fn";
      field(result, current.type().stable_name());
      field(result, reference->symbol().stable_name());
      field(result, reference->signature());
      return remember(std::move(result));
    }
    const auto producer = current.defining_op();
    if (!producer) {
      diagnostics_.report("equivalence encountered an unbound value");
      return std::nullopt;
    }
    return remember(call(*producer, current, parameters, arguments));
  }

  std::optional<std::string> call(const Op& op, const Val& selected,
                                  const std::vector<Val>& parameters,
                                  const std::vector<std::string>& arguments) {
    std::vector<std::pair<Val, std::string>> normalized_arguments;
    const auto call_arguments = op.arguments();
    normalized_arguments.reserve(call_arguments.size());
    for (const Val& argument : call_arguments) {
      auto normalized = value(argument, parameters, arguments);
      if (!normalized) {
        return std::nullopt;
      }
      normalized_arguments.emplace_back(argument, std::move(*normalized));
    }
    std::vector<std::string> residuals;
    residuals.reserve(call_arguments.size());
    for (const Val& argument : call_arguments) {
      const auto normalized = std::find_if(
          normalized_arguments.begin(), normalized_arguments.end(),
          [&](const auto& item) { return item.first == argument; });
      if (normalized == normalized_arguments.end()) {
        diagnostics_.report("equivalence lost a Residual call argument");
        return std::nullopt;
      }
      residuals.push_back(normalized->second);
    }

    const auto results = op.results();
    const auto result = position(results, selected);
    if (!result) {
      diagnostics_.report("equivalence lost a call result");
      return std::nullopt;
    }

    const Val callee_value = op.callee();
    const auto declaration = callee_value.referenced_fn();
    if (!declaration) {
      diagnostics_.report("equivalence does not yet expand a dynamic callee");
      return std::nullopt;
    }
    const Mod::FnDecl callee = *declaration;
    if (callee.form() == Mod::FnDecl::Form::Body) {
      if (expansions_ == max_expansions_) {
        diagnostics_.report("equivalence expansion exceeded " +
                            std::to_string(max_expansions_) + " source calls");
        return std::nullopt;
      }
      const std::string identity = callee.symbol().stable_name();
      if (!active_.insert(identity).second) {
        diagnostics_.report("recursive reference body at '" +
                            callee.symbol().qualified_name() + "'");
        return std::nullopt;
      }
      ++expansions_;
      Diag materialization;
      auto body = compiler_.materialize(op, materialization);
      if (!body) {
        diagnostics_.report("cannot instantiate reference body for '" +
                            callee.symbol().qualified_name() + "'");
        for (const auto& entry : materialization.issues()) {
          diagnostics_.report(entry.message, entry.source);
        }
        active_.erase(identity);
        return std::nullopt;
      }

      Diag eligibility;
      const bool expression = detail::validate_expression_template(
          *body, "reference body", eligibility);
      if (expression) {
        const auto body_arguments = body->arguments();
        if (body_arguments.size() != residuals.size() ||
            body->result_types().size() != 1U || *result != 0U) {
          diagnostics_.report("reference body shape does not match call '" +
                              callee.symbol().qualified_name() + "'");
          active_.erase(identity);
          return std::nullopt;
        }
        auto expanded = value(body->entry().terminator().returned().front(),
                              body_arguments, residuals);
        active_.erase(identity);
        return expanded;
      }
      active_.erase(identity);
    }

    std::string leaf = "call";
    field(leaf, callee.symbol().stable_name());
    field(leaf, callee.signature());
    field(leaf, std::to_string(*result));
    field(leaf, selected.type().stable_name());
    for (const auto& argument : normalized_arguments) {
      field(leaf, argument.second);
    }
    for (const auto& [name, binding] : callee_value.bindings()) {
      const auto normalized = value(binding, parameters, arguments);
      if (!normalized) {
        return std::nullopt;
      }
      field(leaf, name);
      field(leaf, *normalized);
    }
    return leaf;
  }

  Compiler& compiler_;
  std::size_t max_expansions_ = 0;
  Diag& diagnostics_;
  std::size_t expansions_ = 0;
  std::set<std::string> active_;
  std::vector<std::pair<Val, std::string>> memo_;
};

bool same_signature(const Fn& left, const Fn& right) {
  const auto left_arguments = left.arguments();
  const auto right_arguments = right.arguments();
  if (left_arguments.size() != right_arguments.size() ||
      left.result_types().size() != right.result_types().size()) {
    return false;
  }
  for (std::size_t index = 0; index < left_arguments.size(); ++index) {
    if (left_arguments[index].type() != right_arguments[index].type()) {
      return false;
    }
  }
  const auto left_results = left.result_types();
  const auto right_results = right.result_types();
  for (std::size_t index = 0; index < left_results.size(); ++index) {
    if (left_results[index] != right_results[index]) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool equivalent(Compiler& compiler, const Fn& left, const Fn& right,
                Diag& diagnostics, std::size_t max_expansions) {
  if (max_expansions == 0U) {
    diagnostics.report("equivalence needs a positive expansion limit");
    return false;
  }
  try {
    if (!same_signature(left, right)) {
      diagnostics.report("equivalence fns have different signatures");
      return false;
    }
    Normalizer left_normalizer(compiler, max_expansions, diagnostics);
    auto normalized_left = left_normalizer.run(left, "left");
    if (!normalized_left) {
      return false;
    }
    Normalizer right_normalizer(compiler, max_expansions, diagnostics);
    auto normalized_right = right_normalizer.run(right, "right");
    if (!normalized_right) {
      return false;
    }
    if (*normalized_left != *normalized_right) {
      diagnostics.report("fns are not definitionally equivalent");
      return false;
    }
    return true;
  } catch (const std::exception& error) {
    diagnostics.report("equivalence failed: " + std::string(error.what()));
  } catch (...) {
    diagnostics.report("equivalence failed with an unknown exception");
  }
  return false;
}

std::optional<std::size_t> replace(Compiler& compiler, Fn& fn, const Fn& before,
                                   const Fn& after, Diag& diagnostics,
                                   std::size_t max_expansions) {
  if (!equivalent(compiler, before, after, diagnostics, max_expansions)) {
    return std::nullopt;
  }
  return replace(fn, before, after, diagnostics);
}

std::optional<std::size_t> replace(Compiler& compiler, Mod& mod,
                                   const Fn& before, const Fn& after,
                                   Diag& diagnostics,
                                   std::size_t max_expansions) {
  if (!equivalent(compiler, before, after, diagnostics, max_expansions)) {
    return std::nullopt;
  }
  return replace(mod, before, after, diagnostics);
}

}  // namespace joggle
