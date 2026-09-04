#include "joggle/transform.h"

#include "prelude.h"
#include "transform_internal.h"

#include "joggle/compiler.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace joggle {
namespace detail {

bool validate_expression_template(const Fn& fn, std::string_view role,
                                  Diag& diagnostics) {
  const auto reject = [&](std::string reason) {
    diagnostics.report(std::string(role) + " expression template " + reason);
    return false;
  };
  try {
    const auto blocks = fn.blks();
    if (blocks.size() != 1U || !blocks.front().is_entry() ||
        blocks.front().terminator().kind() != Terminator::Kind::Return) {
      return reject("must contain one entry block ending in return");
    }
    const auto returned = blocks.front().terminator().returned();
    const auto result_types = fn.result_types();
    if (result_types.size() != 1U || returned.size() != 1U) {
      return reject("must have exactly one result");
    }

    const auto holes = fn.arguments();
    const auto effect = [](const Val& value) {
      return is_effect_type(value.type());
    };
    if (std::any_of(holes.begin(), holes.end(), effect) ||
        is_effect_type(result_types.front())) {
      return reject("cannot expose an effect token");
    }

    const auto ops = fn.ops();
    for (const Op& op : ops) {
      const auto arguments = op.arguments();
      const auto results = op.results();
      if (results.size() != 1U) {
        return reject("cannot contain a call with multiple results");
      }
      if (std::any_of(arguments.begin(), arguments.end(), effect) ||
          effect(results.front())) {
        return reject("cannot contain an effect token");
      }
      for (const Val& argument : arguments) {
        if (argument.inline_fn()) {
          return reject("cannot contain a nested inline fn");
        }
      }
    }

    std::vector<Op> reachable;
    std::vector<Val> used_holes;
    std::vector<Val> pending{returned.front()};
    while (!pending.empty()) {
      const Val value = pending.back();
      pending.pop_back();
      const auto hole = std::find(holes.begin(), holes.end(), value);
      if (hole != holes.end()) {
        if (std::find(used_holes.begin(), used_holes.end(), value) ==
            used_holes.end()) {
          used_holes.push_back(value);
        }
        continue;
      }
      if (value.known() || value.referenced_fn()) {
        continue;
      }
      const auto producer =
          std::find_if(ops.begin(), ops.end(), [&](const Op& op) {
            const auto results = op.results();
            return std::find(results.begin(), results.end(), value) !=
                   results.end();
          });
      if (producer == ops.end()) {
        return reject("contains a captured residual value");
      }
      if (std::find(reachable.begin(), reachable.end(), *producer) !=
          reachable.end()) {
        continue;
      }
      reachable.push_back(*producer);
      const auto arguments = producer->arguments();
      pending.insert(pending.end(), arguments.begin(), arguments.end());
    }
    if (reachable.size() != ops.size()) {
      return reject("contains a call outside its returned expression");
    }
    if (used_holes.size() != holes.size()) {
      return reject("contains an unused hole");
    }
    return true;
  } catch (const std::exception& error) {
    return reject("validation failed: " + std::string(error.what()));
  } catch (...) {
    return reject("validation failed with an unknown exception");
  }
}

namespace {

struct MatchState {
  std::vector<std::optional<Val>> holes;
  std::vector<std::pair<Op, Op>> calls;
};

std::optional<std::size_t> hole_index(const std::vector<Val>& holes,
                                      const Val& value) {
  const auto found = std::find(holes.begin(), holes.end(), value);
  if (found == holes.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - holes.begin());
}

bool match_value(const Val& pattern, const Val& subject,
                 const std::vector<Val>& holes, MatchState& state) {
  if (pattern.type() != subject.type()) {
    return false;
  }
  if (const auto index = hole_index(holes, pattern)) {
    if (state.holes[*index]) {
      return *state.holes[*index] == subject;
    }
    state.holes[*index] = subject;
    return true;
  }
  if (pattern.known()) {
    return pattern == subject;
  }
  if (const auto reference = pattern.referenced_fn()) {
    return subject.referenced_fn() == reference;
  }

  const auto pattern_call = pattern.defining_op();
  const auto subject_call = subject.defining_op();
  if (!pattern_call || !subject_call ||
      pattern_call->callee() != subject_call->callee()) {
    return false;
  }
  const auto pattern_results = pattern_call->results();
  const auto subject_results = subject_call->results();
  const auto pattern_result =
      std::find(pattern_results.begin(), pattern_results.end(), pattern);
  const auto subject_result =
      std::find(subject_results.begin(), subject_results.end(), subject);
  if (pattern_results.size() != subject_results.size() ||
      pattern_result == pattern_results.end() ||
      subject_result == subject_results.end() ||
      pattern_result - pattern_results.begin() !=
          subject_result - subject_results.begin()) {
    return false;
  }
  const auto existing = std::find_if(state.calls.begin(), state.calls.end(),
                                     [&](const auto& mapping) {
                                       return mapping.first == *pattern_call ||
                                              mapping.second == *subject_call;
                                     });
  if (existing != state.calls.end()) {
    return existing->first == *pattern_call &&
           existing->second == *subject_call;
  }
  state.calls.emplace_back(*pattern_call, *subject_call);

  const auto pattern_arguments = pattern_call->arguments();
  const auto subject_arguments = subject_call->arguments();
  if (pattern_arguments.size() != subject_arguments.size()) {
    return false;
  }
  for (std::size_t index = 0; index < pattern_arguments.size(); ++index) {
    if (!match_value(pattern_arguments[index], subject_arguments[index], holes,
                     state)) {
      return false;
    }
  }
  return true;
}

bool terminator_uses(const Fn& fn, const Val& value) {
  for (const Blk& block : fn.blks()) {
    const Terminator terminator = block.terminator();
    const auto returned = terminator.returned();
    if (terminator.condition() == std::optional<Val>{value} ||
        std::find(returned.begin(), returned.end(), value) != returned.end()) {
      return true;
    }
    for (std::size_t successor = 0; successor < terminator.successor_count();
         ++successor) {
      const auto arguments = terminator.arguments(successor);
      if (std::find(arguments.begin(), arguments.end(), value) !=
          arguments.end()) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::optional<std::vector<ExprMatch>>
match_expressions(const Fn& subject, const Fn& pattern, Diag& diagnostics) {
  if (!validate_expression_template(pattern, "before", diagnostics)) {
    return std::nullopt;
  }
  try {
    const auto holes = pattern.arguments();
    const Val pattern_root = pattern.entry().terminator().returned().front();
    const auto subject_calls = subject.ops();
    std::vector<ExprMatch> matches;
    for (const Op& candidate : subject_calls) {
      for (const Val& root : candidate.results()) {
        MatchState state{std::vector<std::optional<Val>>(holes.size()), {}};
        if (!match_value(pattern_root, root, holes, state)) {
          continue;
        }
        std::vector<Op> calls;
        for (const Op& call : subject_calls) {
          if (std::any_of(state.calls.begin(), state.calls.end(),
                          [&](const auto& mapping) {
                            return mapping.second == call;
                          })) {
            calls.push_back(call);
          }
        }
        std::vector<Val> bindings;
        bindings.reserve(state.holes.size());
        bool complete = true;
        for (const auto& binding : state.holes) {
          if (!binding) {
            complete = false;
            break;
          }
          bindings.push_back(*binding);
        }
        if (complete) {
          matches.push_back({root, std::move(bindings), std::move(calls)});
        }
      }
    }
    return matches;
  } catch (const std::exception& error) {
    diagnostics.report("expression matching failed: " +
                       std::string(error.what()));
  } catch (...) {
    diagnostics.report("expression matching failed with an unknown exception");
  }
  return std::nullopt;
}

std::optional<std::size_t>
replace_expressions(Fn& subject, const Fn& before, const Fn& after,
                    Diag& diagnostics, std::span<const Val> allowed_roots) {
  if (!validate_expression_template(before, "before", diagnostics) ||
      !validate_expression_template(after, "after", diagnostics)) {
    return std::nullopt;
  }
  const auto before_arguments = before.arguments();
  const auto after_arguments = after.arguments();
  if (before_arguments.size() != after_arguments.size() ||
      before.result_types() != after.result_types()) {
    diagnostics.report(
        "before and after expression templates have different signatures");
    return std::nullopt;
  }
  for (std::size_t index = 0; index < before_arguments.size(); ++index) {
    if (before_arguments[index].type() != after_arguments[index].type()) {
      diagnostics.report(
          "before and after expression templates have different signatures");
      return std::nullopt;
    }
  }

  auto matches = match_expressions(subject, before, diagnostics);
  if (!matches) {
    return std::nullopt;
  }
  std::vector<ExprMatch> selected;
  std::vector<Op> claimed;
  for (const ExprMatch& match : *matches) {
    if (!allowed_roots.empty() &&
        std::find(allowed_roots.begin(), allowed_roots.end(), match.root) ==
            allowed_roots.end()) {
      continue;
    }
    const bool overlaps = std::any_of(
        match.calls.begin(), match.calls.end(), [&](const Op& call) {
          return std::find(claimed.begin(), claimed.end(), call) !=
                 claimed.end();
        });
    if (overlaps) {
      continue;
    }
    selected.push_back(match);
    claimed.insert(claimed.end(), match.calls.begin(), match.calls.end());
  }
  if (selected.empty()) {
    return 0U;
  }

  const auto claimed_call = [&](const Op& call) {
    return std::find(claimed.begin(), claimed.end(), call) != claimed.end();
  };
  const auto replaced_root = [&](const Val& value) {
    return std::any_of(
        selected.begin(), selected.end(),
        [&](const ExprMatch& match) { return match.root == value; });
  };
  std::vector<Op> preserved;
  for (const Op& call : claimed) {
    const auto results = call.results();
    const bool escapes =
        std::any_of(results.begin(), results.end(), [&](const Val& result) {
          if (replaced_root(result)) {
            return false;
          }
          const auto users = subject.users(result);
          return terminator_uses(subject, result) ||
                 std::any_of(users.begin(), users.end(), [&](const Op& user) {
                   return !claimed_call(user);
                 });
        });
    if (escapes) {
      preserved.push_back(call);
    }
  }
  for (std::size_t index = 0; index < preserved.size(); ++index) {
    for (const Val& argument : preserved[index].arguments()) {
      const auto producer = argument.defining_op();
      if (producer && claimed_call(*producer) &&
          std::find(preserved.begin(), preserved.end(), *producer) ==
              preserved.end()) {
        preserved.push_back(*producer);
      }
    }
  }

  try {
    auto edit = subject.edit();
    const auto after_calls = after.ops();
    const Val after_root = after.entry().terminator().returned().front();
    std::vector<std::pair<Val, Val>> roots;
    for (const ExprMatch& match : selected) {
      std::vector<std::pair<Val, Val>> values;
      for (std::size_t index = 0; index < after_arguments.size(); ++index) {
        values.emplace_back(after_arguments[index], match.bindings[index]);
      }
      const auto map_value = [&](const Val& value) -> std::optional<Val> {
        if (value.known()) {
          return value;
        }
        const auto mapped =
            std::find_if(values.begin(), values.end(),
                         [&](const auto& item) { return item.first == value; });
        if (mapped != values.end()) {
          return mapped->second;
        }
        if (const auto reference = value.referenced_fn()) {
          const Val cloned = edit.reference(*reference, value.type());
          values.emplace_back(value, cloned);
          return cloned;
        }
        return std::nullopt;
      };

      const auto insertion = match.root.defining_op();
      if (!insertion) {
        diagnostics.report("matched expression root is not a call result");
        return std::nullopt;
      }
      for (const Op& call : after_calls) {
        std::vector<Val> arguments;
        for (const Val& argument : call.arguments()) {
          const auto mapped = map_value(argument);
          if (!mapped) {
            diagnostics.report("replacement expression lost a value mapping");
            return std::nullopt;
          }
          arguments.push_back(*mapped);
        }
        std::vector<Type> result_types;
        for (const Val& result : call.results()) {
          result_types.push_back(result.type());
        }
        const Op cloned = edit.insert(*insertion, call.callee(),
                                      std::move(arguments), result_types);
        if (const auto location = insertion->location()) {
          edit.locate(cloned, *location);
        }
        const auto original_results = call.results();
        const auto cloned_results = cloned.results();
        for (std::size_t index = 0; index < original_results.size(); ++index) {
          values.emplace_back(original_results[index], cloned_results[index]);
        }
      }
      const auto replacement = map_value(after_root);
      if (!replacement) {
        diagnostics.report("replacement expression has no mapped root");
        return std::nullopt;
      }
      roots.emplace_back(match.root, *replacement);
    }

    for (const auto& [root, replacement] : roots) {
      edit.replace(root, replacement);
    }
    const auto subject_calls = subject.ops();
    for (auto call = subject_calls.rbegin(); call != subject_calls.rend();
         ++call) {
      if (claimed_call(*call) && std::find(preserved.begin(), preserved.end(),
                                           *call) == preserved.end()) {
        edit.erase(*call);
      }
    }
    return edit.commit(diagnostics)
               ? std::optional<std::size_t>{selected.size()}
               : std::nullopt;
  } catch (const std::exception& error) {
    diagnostics.report("expression replacement failed: " +
                       std::string(error.what()));
  } catch (...) {
    diagnostics.report(
        "expression replacement failed with an unknown exception");
  }
  return std::nullopt;
}

}  // namespace detail
namespace {

template <typename From, typename To>
std::optional<To> mapped(const std::vector<std::pair<From, To>>& values,
                         const From& value) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto& item) { return item.first == value; });
  return found == values.end() ? std::nullopt
                               : std::optional<To>{found->second};
}

}  // namespace

std::optional<Fn>
clone(Compiler& compiler, const Fn& source,
      const std::function<std::optional<Type>(const Val&)>& map_value_type,
      const std::function<std::optional<Mod::FnDecl>(const Op&)>& map_callee,
      Diag& diagnostics) {
  try {
    auto destination = compiler.create_fn();
    if (!destination) {
      return std::nullopt;
    }
    auto edit = destination->edit();
    std::vector<std::pair<Val, Val>> values;
    std::vector<std::pair<Blk, Blk>> blocks;

    const auto convert_type = [&](const Val& value) -> std::optional<Type> {
      const auto converted = map_value_type(value);
      if (!converted) {
        diagnostics.report("clone has no mapping for type '" +
                           value.type().schema().symbol().qualified_name() +
                           "'");
      }
      return converted;
    };

    for (const Val& argument : source.arguments()) {
      const auto type = convert_type(argument);
      if (!type) {
        return std::nullopt;
      }
      values.emplace_back(argument, edit.argument(*type));
    }

    const auto source_blocks = source.blks();
    for (std::size_t index = 0; index < source_blocks.size(); ++index) {
      std::vector<Type> argument_types;
      for (const Val& argument : source_blocks[index].arguments()) {
        const auto type = convert_type(argument);
        if (!type) {
          return std::nullopt;
        }
        argument_types.push_back(*type);
      }
      Blk block = index == 0U ? destination->entry()
                              : edit.blk(std::move(argument_types));
      if (index == 0U && !argument_types.empty()) {
        diagnostics.report("clone encountered entry Blk arguments");
        return std::nullopt;
      }
      blocks.emplace_back(source_blocks[index], block);
      const auto target_arguments = block.arguments();
      const auto source_arguments = source_blocks[index].arguments();
      for (std::size_t argument = 0; argument < source_arguments.size();
           ++argument) {
        values.emplace_back(source_arguments[argument],
                            target_arguments[argument]);
      }
    }

    const auto convert_value = [&](const Val& value) -> std::optional<Val> {
      if (value.known()) {
        return value;
      }
      if (const auto existing = mapped(values, value)) {
        return existing;
      }
      const auto reference = value.referenced_fn();
      const auto type = reference ? convert_type(value) : std::optional<Type>{};
      if (reference && type) {
        const Val converted = edit.reference(*reference, *type);
        values.emplace_back(value, converted);
        return converted;
      }
      const auto inline_fn = value.inline_fn();
      const auto inline_type =
          inline_fn ? convert_type(value) : std::optional<Type>{};
      if (inline_fn && inline_type) {
        auto converted_fn = clone(compiler, *inline_fn, map_value_type,
                                  map_callee, diagnostics);
        if (!converted_fn) {
          return std::nullopt;
        }
        const Val converted =
            edit.callable(std::move(*converted_fn), *inline_type);
        values.emplace_back(value, converted);
        return converted;
      }
      diagnostics.report("clone encountered a value before its definition");
      return std::nullopt;
    };

    for (const Blk& source_block : source_blocks) {
      const auto target_block = mapped(blocks, source_block);
      if (!target_block) {
        diagnostics.report("clone lost a Blk mapping");
        return std::nullopt;
      }
      for (const Op& op : source_block.ops()) {
        const auto callee = map_callee(op);
        if (!callee) {
          diagnostics.report("clone has no mapping for Op '" +
                             op.callee().symbol().qualified_name() + "'");
          return std::nullopt;
        }
        std::vector<Val> arguments;
        for (const Val& argument : op.arguments()) {
          const auto converted = convert_value(argument);
          if (!converted) {
            return std::nullopt;
          }
          arguments.push_back(*converted);
        }
        std::vector<Type> result_types;
        for (const Val& result : op.results()) {
          const auto type = convert_type(result);
          if (!type) {
            return std::nullopt;
          }
          result_types.push_back(*type);
        }
        const Op converted =
            edit.append(*target_block, *callee, std::move(arguments),
                        std::move(result_types));
        if (const auto location = op.location()) {
          edit.locate(converted, *location);
        }
        const auto source_results = op.results();
        const auto target_results = converted.results();
        for (std::size_t result = 0; result < source_results.size(); ++result) {
          values.emplace_back(source_results[result], target_results[result]);
        }
      }
    }

    for (const Blk& source_block : source_blocks) {
      const Blk target_block = *mapped(blocks, source_block);
      const Terminator terminator = source_block.terminator();
      if (terminator.kind() == Terminator::Kind::Return) {
        std::vector<Val> returned;
        for (const Val& value : terminator.returned()) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          returned.push_back(*converted);
        }
        edit.ret(target_block, std::move(returned));
      } else if (terminator.kind() == Terminator::Kind::Jump) {
        std::vector<Val> arguments;
        for (const Val& value : terminator.arguments(0U)) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          arguments.push_back(*converted);
        }
        edit.jump(target_block, *mapped(blocks, terminator.successor(0U)),
                  std::move(arguments));
      } else {
        const auto condition = terminator.condition();
        if (!condition) {
          diagnostics.report("clone found a branch without a condition");
          return std::nullopt;
        }
        const auto converted_condition = convert_value(*condition);
        std::vector<Val> true_arguments;
        std::vector<Val> false_arguments;
        for (const Val& value : terminator.arguments(0U)) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          true_arguments.push_back(*converted);
        }
        for (const Val& value : terminator.arguments(1U)) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          false_arguments.push_back(*converted);
        }
        if (!converted_condition) {
          return std::nullopt;
        }
        edit.branch(target_block, *converted_condition,
                    *mapped(blocks, terminator.successor(0U)),
                    std::move(true_arguments),
                    *mapped(blocks, terminator.successor(1U)),
                    std::move(false_arguments));
      }
    }
    return edit.commit(diagnostics) ? std::move(destination) : std::nullopt;
  } catch (const std::exception& error) {
    diagnostics.report("clone failed: " + std::string(error.what()));
  } catch (...) {
    diagnostics.report("clone failed with an unknown exception");
  }
  return std::nullopt;
}

std::optional<Fn>
clone(Compiler& compiler, const Fn& source,
      const std::function<std::optional<Type>(const Val&)>& map_value_type,
      Diag& diagnostics) {
  return clone(
      compiler, source, map_value_type,
      [](const Op& op) -> std::optional<Mod::FnDecl> { return op.callee(); },
      diagnostics);
}

std::optional<std::size_t> replace(Fn& fn, const Fn& before, const Fn& after,
                                   Diag& diagnostics) {
  return detail::replace_expressions(fn, before, after, diagnostics);
}

std::optional<std::size_t> replace(Mod& mod, const Fn& before, const Fn& after,
                                   Diag& diagnostics) {
  try {
    Mod candidate = mod;
    std::size_t changed = 0;
    for (const Mod::FnDecl& member : mod.fns()) {
      const Fn* source = member.body();
      if (source == nullptr) {
        continue;
      }
      Fn rewritten = *source;
      const auto count =
          detail::replace_expressions(rewritten, before, after, diagnostics);
      if (!count) {
        return std::nullopt;
      }
      if (*count != 0U) {
        Fn* destination = candidate.body(member);
        if (destination == nullptr) {
          diagnostics.report("Mod lost fn '" + std::string(member.name()) +
                             "'");
          return std::nullopt;
        }
        *destination = std::move(rewritten);
      }
      changed += *count;
    }
    if (changed != 0U) {
      mod = std::move(candidate);
    }
    return changed;
  } catch (const std::exception& error) {
    diagnostics.report("Mod expression replacement failed: " +
                       std::string(error.what()));
  } catch (...) {
    diagnostics.report(
        "Mod expression replacement failed with an unknown exception");
  }
  return std::nullopt;
}

}  // namespace joggle
