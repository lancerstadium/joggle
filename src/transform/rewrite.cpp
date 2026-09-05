#include "joggle/transform.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joggle/compiler.h"
#include "lang/prelude.h"
#include "transform/clone.h"
#include "transform/nested.h"

namespace joggle {
namespace {

bool contains(const std::vector<Val>& values, const Val& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool contains(const std::vector<Op>& values, const Op& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool has_effect(const Type& type) {
  const auto symbol = type.schema().symbol();
  if (symbol.mod_name() == detail::prelude_mod_name &&
      symbol.local_name() == "effect") {
    return true;
  }
  if (symbol.mod_name() != detail::prelude_mod_name ||
      symbol.local_name() != "callable") {
    return false;
  }
  const auto inputs = type.get<std::vector<Type>>("inputs");
  const auto results = type.get<std::vector<Type>>("results");
  return (inputs && std::any_of(inputs->begin(), inputs->end(), has_effect)) ||
         (results && std::any_of(results->begin(), results->end(), has_effect));
}

bool has_effect(const Fn& fn) {
  for (const Val& argument : fn.arguments()) {
    if (has_effect(argument.type())) {
      return true;
    }
  }
  for (const Type& result : fn.result_types()) {
    if (has_effect(result)) {
      return true;
    }
  }
  for (const Op& op : fn.ops()) {
    if (has_effect(op.callee().type()) || op.results().empty()) {
      return true;
    }
  }
  return false;
}

std::optional<std::size_t> result_index(const Op& op, const Val& value) {
  const auto results = op.results();
  const auto found = std::find(results.begin(), results.end(), value);
  return found == results.end()
             ? std::nullopt
             : std::optional<std::size_t>{static_cast<std::size_t>(
                   std::distance(results.begin(), found))};
}

struct Match {
  detail::ValMap arguments;
  std::vector<std::pair<Op, Op>> ops;
  Val root;
};

std::optional<Val> bound(const detail::ValMap& values, const Val& pattern) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto& item) { return item.first == pattern; });
  return found == values.end() ? std::nullopt
                               : std::optional<Val>{found->second};
}

bool match_value(const Val& pattern, const Val& target, Match& match,
                 const Blk& root_block);

bool match_call(const Op& pattern, const Op& target, Match& match,
                const Blk& root_block) {
  const auto existing =
      std::find_if(match.ops.begin(), match.ops.end(),
                   [&](const auto& item) { return item.first == pattern; });
  if (existing != match.ops.end()) {
    return existing->second == target;
  }
  if (target.parent() != root_block ||
      pattern.arguments().size() != target.arguments().size() ||
      pattern.results().size() != target.results().size()) {
    return false;
  }
  match.ops.emplace_back(pattern, target);
  if (!match_value(pattern.callee(), target.callee(), match, root_block)) {
    return false;
  }
  const auto pattern_arguments = pattern.arguments();
  const auto target_arguments = target.arguments();
  for (std::size_t index = 0; index < pattern_arguments.size(); ++index) {
    if (!match_value(pattern_arguments[index], target_arguments[index], match,
                     root_block)) {
      return false;
    }
  }
  return true;
}

bool match_reference(const Val& pattern, const Val& target) {
  const auto pattern_fn = pattern.referenced_fn();
  const auto target_fn = target.referenced_fn();
  if (!pattern_fn || !target_fn || *pattern_fn != *target_fn ||
      pattern.bindings().size() != target.bindings().size()) {
    return false;
  }
  const auto left = pattern.bindings();
  const auto right = target.bindings();
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].first != right[index].first ||
        left[index].second != right[index].second) {
      return false;
    }
  }
  return true;
}

bool match_value(const Val& pattern, const Val& target, Match& match,
                 const Blk& root_block) {
  if (pattern.type() != target.type()) {
    return false;
  }
  if (pattern.is_fn_arg()) {
    if (const auto existing = bound(match.arguments, pattern)) {
      return *existing == target;
    }
    match.arguments.emplace_back(pattern, target);
    return true;
  }
  if (pattern.known() || target.known()) {
    return pattern == target;
  }
  if (pattern.referenced_fn()) {
    return match_reference(pattern, target);
  }
  if (pattern.inline_fn()) {
    return false;
  }
  const auto pattern_op = pattern.defining_op();
  const auto target_op = target.defining_op();
  if (!pattern_op || !target_op) {
    return false;
  }
  const auto pattern_result = result_index(*pattern_op, pattern);
  const auto target_result = result_index(*target_op, target);
  return pattern_result && target_result && *pattern_result == *target_result &&
         match_call(*pattern_op, *target_op, match, root_block);
}

bool mark(std::vector<Val>& live, const Val& value) {
  if (contains(live, value)) {
    return false;
  }
  live.push_back(value);
  for (const Val& capture : value.captures()) {
    mark(live, capture);
  }
  return true;
}

std::optional<std::size_t> erase_dead(Fn& fn, Diag& diagnostics) {
  std::vector<Val> live;
  for (const Blk& block : fn.blks()) {
    const Term term = block.terminator();
    if (const auto condition = term.condition()) {
      mark(live, *condition);
    }
    for (const Val& value : term.returned()) {
      mark(live, value);
    }
    for (std::size_t successor = 0; successor < term.successor_count();
         ++successor) {
      for (const Val& value : term.arguments(successor)) {
        mark(live, value);
      }
    }
  }

  std::vector<Op> dead;
  const auto operations = fn.ops();
  for (auto item = operations.rbegin(); item != operations.rend(); ++item) {
    const auto results = item->results();
    const bool required =
        results.empty() || has_effect(item->callee().type()) ||
        std::any_of(results.begin(), results.end(),
                    [&](const Val& result) { return contains(live, result); });
    if (!required) {
      dead.push_back(*item);
      continue;
    }
    mark(live, item->callee());
    for (const Val& argument : item->arguments()) {
      mark(live, argument);
    }
  }
  if (dead.empty()) {
    return std::size_t{0};
  }
  auto edit = fn.edit();
  for (const Op& op : dead) {
    edit.erase(op);
  }
  if (!edit.commit(diagnostics)) {
    return std::nullopt;
  }
  return dead.size();
}

bool valid_rule(const Fn& before, const Fn& after, Diag& diagnostics) {
  if (before.blks().size() != 1U || after.blks().size() != 1U ||
      before.entry().terminator().kind() != Term::Kind::Return ||
      after.entry().terminator().kind() != Term::Kind::Return) {
    diagnostics.report("pass equations must be single-block fns");
    return false;
  }
  if (before.arguments().size() != after.arguments().size() ||
      before.result_types() != after.result_types()) {
    diagnostics.report("pass equations must have the same typed signature");
    return false;
  }
  for (std::size_t index = 0; index < before.arguments().size(); ++index) {
    if (before.arguments()[index].type() != after.arguments()[index].type()) {
      diagnostics.report("pass equation argument Types differ");
      return false;
    }
  }
  if (before.entry().terminator().returned().size() != 1U ||
      after.entry().terminator().returned().size() != 1U) {
    diagnostics.report("pass equations must return exactly one value");
    return false;
  }
  if (has_effect(before) || has_effect(after)) {
    diagnostics.report("pass equations cannot contain effects");
    return false;
  }
  if (!before.entry().terminator().returned().front().defining_op()) {
    diagnostics.report("a pass pattern must return an expression");
    return false;
  }
  return true;
}

std::optional<std::size_t> apply_pass_impl(Compiler& compiler, Fn& fn,
                                           const Fn& before, const Fn& after,
                                           Diag& diagnostics) {
  struct Nested {
    Val value;
    Fn body;
  };
  std::vector<Nested> nested;
  std::size_t total = 0;
  for (const Val& value : detail::nested_values(fn)) {
    auto body = value.inline_fn();
    if (!body) {
      continue;
    }
    const auto changed =
        apply_pass_impl(compiler, *body, before, after, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    if (*changed != 0U) {
      total += *changed;
      nested.push_back({value, std::move(*body)});
    }
  }
  if (!nested.empty()) {
    auto edit = fn.edit();
    for (Nested& item : nested) {
      Val replacement = edit.callable(std::move(item.body), item.value.type(),
                                      item.value.captures());
      edit.replace(item.value, replacement);
    }
    if (!edit.commit(compiler, diagnostics)) {
      return std::nullopt;
    }
  }

  const Val pattern_root = before.entry().terminator().returned().front();
  std::vector<Match> matches;
  std::vector<Op> claimed;
  for (const Op& operation : fn.ops()) {
    for (const Val& candidate : operation.results()) {
      Match match{{}, {}, candidate};
      if (!match_value(pattern_root, candidate, match, operation.parent())) {
        continue;
      }
      const bool overlaps = std::any_of(
          match.ops.begin(), match.ops.end(),
          [&](const auto& item) { return contains(claimed, item.second); });
      if (overlaps) {
        continue;
      }
      for (const auto& item : match.ops) {
        claimed.push_back(item.second);
      }
      matches.push_back(std::move(match));
    }
  }
  if (matches.empty()) {
    return total;
  }

  auto edit = fn.edit();
  for (Match& match : matches) {
    detail::ValMap values;
    const auto before_arguments = before.arguments();
    const auto after_arguments = after.arguments();
    for (std::size_t index = 0; index < before_arguments.size(); ++index) {
      const auto value = bound(match.arguments, before_arguments[index]);
      if (!value) {
        diagnostics.report("a pass replacement uses an unbound argument");
        return std::nullopt;
      }
      values.emplace_back(after_arguments[index], *value);
    }
    const Op root = *match.root.defining_op();
    const auto returned = detail::clone_before(edit, after, root, values,
                                               diagnostics, root.location());
    if (!returned || returned->size() != 1U) {
      return std::nullopt;
    }
    edit.replace(match.root, returned->front());
  }
  if (!edit.commit(compiler, diagnostics)) {
    return std::nullopt;
  }
  if (!erase_dead(fn, diagnostics)) {
    return std::nullopt;
  }
  return total + matches.size();
}

}  // namespace

std::optional<std::size_t> apply_pass(Compiler& compiler, Fn& fn,
                                      const Fn& before, const Fn& after,
                                      Diag& diagnostics) {
  if (!valid_rule(before, after, diagnostics)) {
    return std::nullopt;
  }
  Fn candidate = fn;
  const auto changed =
      apply_pass_impl(compiler, candidate, before, after, diagnostics);
  if (changed && *changed != 0U) {
    fn = std::move(candidate);
  }
  return changed;
}

}  // namespace joggle
