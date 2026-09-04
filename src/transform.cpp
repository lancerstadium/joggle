#include "joggle/transform.h"

#include "prelude.h"
#include "transform_internal.h"

#include "joggle/compiler.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace joggle {
namespace detail {

bool validate_expression_template(const Function& function,
                                  std::string_view role,
                                  Diagnostics& diagnostics) {
  const auto reject = [&](std::string reason) {
    diagnostics.report(std::string(role) + " expression template " + reason);
    return false;
  };
  try {
    const auto blocks = function.blocks();
    if (blocks.size() != 1U || !blocks.front().is_entry() ||
        blocks.front().terminator().kind() != Terminator::Kind::Return) {
      return reject("must contain one entry block ending in return");
    }
    const auto returned = blocks.front().terminator().returned();
    const auto result_types = function.result_types();
    if (result_types.size() != 1U || returned.size() != 1U) {
      return reject("must have exactly one result");
    }

    const auto holes = function.arguments();
    const auto effect = [](const Value& value) {
      return is_effect_type(value.type());
    };
    if (std::any_of(holes.begin(), holes.end(), effect) ||
        is_effect_type(result_types.front())) {
      return reject("cannot expose an effect token");
    }

    const auto ops = function.ops();
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
      for (const Value& argument : arguments) {
        if (argument.inline_function()) {
          return reject("cannot contain a nested inline function");
        }
      }
    }

    std::vector<Op> reachable;
    std::vector<Value> used_holes;
    std::vector<Value> pending{returned.front()};
    while (!pending.empty()) {
      const Value value = pending.back();
      pending.pop_back();
      const auto hole = std::find(holes.begin(), holes.end(), value);
      if (hole != holes.end()) {
        if (std::find(used_holes.begin(), used_holes.end(), value) ==
            used_holes.end()) {
          used_holes.push_back(value);
        }
        continue;
      }
      if (value.known() || value.referenced_function()) {
        continue;
      }
      const auto producer = std::find_if(
          ops.begin(), ops.end(), [&](const Op& op) {
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
  std::vector<std::optional<Value>> holes;
  std::vector<std::pair<Op, Op>> calls;
};

std::optional<std::size_t> hole_index(const std::vector<Value>& holes,
                                      const Value& value) {
  const auto found = std::find(holes.begin(), holes.end(), value);
  if (found == holes.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - holes.begin());
}

bool match_value(const Value& pattern, const Value& subject,
                 const std::vector<Value>& holes, MatchState& state) {
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
  if (const auto reference = pattern.referenced_function()) {
    return subject.referenced_function() == reference;
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
  const auto existing = std::find_if(
      state.calls.begin(), state.calls.end(), [&](const auto& mapping) {
        return mapping.first == *pattern_call || mapping.second == *subject_call;
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

bool terminator_uses(const Function& function, const Value& value) {
  for (const Block& block : function.blocks()) {
    const Terminator terminator = block.terminator();
    const auto returned = terminator.returned();
    if (terminator.condition() == std::optional<Value>{value} ||
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

std::optional<std::vector<ExpressionMatch>>
match_expressions(const Function& subject, const Function& pattern,
                  Diagnostics& diagnostics) {
  if (!validate_expression_template(pattern, "before", diagnostics)) {
    return std::nullopt;
  }
  try {
    const auto holes = pattern.arguments();
    const Value pattern_root = pattern.entry().terminator().returned().front();
    const auto subject_calls = subject.ops();
    std::vector<ExpressionMatch> matches;
    for (const Op& candidate : subject_calls) {
      for (const Value& root : candidate.results()) {
        MatchState state{
            std::vector<std::optional<Value>>(holes.size()), {}};
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
        std::vector<Value> bindings;
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
replace_expressions(Function& subject, const Function& before,
                    const Function& after, Diagnostics& diagnostics,
                    std::span<const Value> allowed_roots) {
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
  std::vector<ExpressionMatch> selected;
  std::vector<Op> claimed;
  for (const ExpressionMatch& match : *matches) {
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
  const auto replaced_root = [&](const Value& value) {
    return std::any_of(selected.begin(), selected.end(),
                       [&](const ExpressionMatch& match) {
                         return match.root == value;
                       });
  };
  std::vector<Op> preserved;
  for (const Op& call : claimed) {
    const auto results = call.results();
    const bool escapes = std::any_of(
        results.begin(), results.end(), [&](const Value& result) {
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
    for (const Value& argument : preserved[index].arguments()) {
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
    const Value after_root = after.entry().terminator().returned().front();
    std::vector<std::pair<Value, Value>> roots;
    for (const ExpressionMatch& match : selected) {
      std::vector<std::pair<Value, Value>> values;
      for (std::size_t index = 0; index < after_arguments.size(); ++index) {
        values.emplace_back(after_arguments[index], match.bindings[index]);
      }
      const auto map_value = [&](const Value& value) -> std::optional<Value> {
        if (value.known()) {
          return value;
        }
        const auto mapped = std::find_if(
            values.begin(), values.end(), [&](const auto& item) {
              return item.first == value;
            });
        if (mapped != values.end()) {
          return mapped->second;
        }
        if (const auto reference = value.referenced_function()) {
          const Value cloned = edit.reference(*reference, value.type());
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
        std::vector<Value> arguments;
        for (const Value& argument : call.arguments()) {
          const auto mapped = map_value(argument);
          if (!mapped) {
            diagnostics.report("replacement expression lost a value mapping");
            return std::nullopt;
          }
          arguments.push_back(*mapped);
        }
        std::vector<Type> result_types;
        for (const Value& result : call.results()) {
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
      if (claimed_call(*call) &&
          std::find(preserved.begin(), preserved.end(), *call) ==
              preserved.end()) {
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
  const auto found = std::find_if(values.begin(), values.end(),
                                  [&](const auto& item) {
                                    return item.first == value;
                                  });
  return found == values.end() ? std::nullopt
                               : std::optional<To>{found->second};
}

}  // namespace

std::optional<Function> clone(
    Compiler& compiler, const Function& source,
    const std::function<std::optional<Type>(const Value&)>& map_value_type,
    const std::function<std::optional<Module::FunctionDecl>(const Op&)>&
        map_callee,
    Diagnostics& diagnostics) {
  try {
    auto destination = compiler.create_function();
    if (!destination) {
      return std::nullopt;
    }
    auto edit = destination->edit();
    std::vector<std::pair<Value, Value>> values;
    std::vector<std::pair<Block, Block>> blocks;

    const auto convert_type = [&](const Value& value) -> std::optional<Type> {
      const auto converted = map_value_type(value);
      if (!converted) {
        diagnostics.report("clone has no mapping for type '" +
                           value.type().schema().symbol().qualified_name() +
                           "'");
      }
      return converted;
    };

    for (const Value& argument : source.arguments()) {
      const auto type = convert_type(argument);
      if (!type) {
        return std::nullopt;
      }
      values.emplace_back(argument, edit.argument(*type));
    }

    const auto source_blocks = source.blocks();
    for (std::size_t index = 0; index < source_blocks.size(); ++index) {
      std::vector<Type> argument_types;
      for (const Value& argument : source_blocks[index].arguments()) {
        const auto type = convert_type(argument);
        if (!type) {
          return std::nullopt;
        }
        argument_types.push_back(*type);
      }
      Block block = index == 0U ? destination->entry()
                                : edit.block(std::move(argument_types));
      if (index == 0U && !argument_types.empty()) {
        diagnostics.report("clone encountered entry Block arguments");
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

    const auto convert_value = [&](const Value& value)
        -> std::optional<Value> {
      if (value.known()) {
        return value;
      }
      if (const auto existing = mapped(values, value)) {
        return existing;
      }
      const auto reference = value.referenced_function();
      const auto type = reference ? convert_type(value)
                                  : std::optional<Type>{};
      if (reference && type) {
        const Value converted = edit.reference(*reference, *type);
        values.emplace_back(value, converted);
        return converted;
      }
      const auto inline_function = value.inline_function();
      const auto inline_type = inline_function ? convert_type(value)
                                               : std::optional<Type>{};
      if (inline_function && inline_type) {
        auto converted_function = clone(compiler, *inline_function,
                                        map_value_type, map_callee,
                                        diagnostics);
        if (!converted_function) {
          return std::nullopt;
        }
        const Value converted =
            edit.callable(std::move(*converted_function), *inline_type);
        values.emplace_back(value, converted);
        return converted;
      }
      diagnostics.report("clone encountered a value before its definition");
      return std::nullopt;
    };

    for (const Block& source_block : source_blocks) {
      const auto target_block = mapped(blocks, source_block);
      if (!target_block) {
        diagnostics.report("clone lost a Block mapping");
        return std::nullopt;
      }
      for (const Op& op : source_block.ops()) {
        const auto callee = map_callee(op);
        if (!callee) {
          diagnostics.report("clone has no mapping for Op '" +
                             op.callee().symbol().qualified_name() + "'");
          return std::nullopt;
        }
        std::vector<Value> arguments;
        for (const Value& argument : op.arguments()) {
          const auto converted = convert_value(argument);
          if (!converted) {
            return std::nullopt;
          }
          arguments.push_back(*converted);
        }
        std::vector<Type> result_types;
        for (const Value& result : op.results()) {
          const auto type = convert_type(result);
          if (!type) {
            return std::nullopt;
          }
          result_types.push_back(*type);
        }
        const Op converted = edit.append(*target_block, *callee,
                                         std::move(arguments),
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

    for (const Block& source_block : source_blocks) {
      const Block target_block = *mapped(blocks, source_block);
      const Terminator terminator = source_block.terminator();
      if (terminator.kind() == Terminator::Kind::Return) {
        std::vector<Value> returned;
        for (const Value& value : terminator.returned()) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          returned.push_back(*converted);
        }
        edit.ret(target_block, std::move(returned));
      } else if (terminator.kind() == Terminator::Kind::Jump) {
        std::vector<Value> arguments;
        for (const Value& value : terminator.arguments(0U)) {
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
        std::vector<Value> true_arguments;
        std::vector<Value> false_arguments;
        for (const Value& value : terminator.arguments(0U)) {
          const auto converted = convert_value(value);
          if (!converted) {
            return std::nullopt;
          }
          true_arguments.push_back(*converted);
        }
        for (const Value& value : terminator.arguments(1U)) {
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

std::optional<Function> clone(
    Compiler& compiler, const Function& source,
    const std::function<std::optional<Type>(const Value&)>& map_value_type,
    Diagnostics& diagnostics) {
  return clone(
      compiler, source, map_value_type,
      [](const Op& op) -> std::optional<Module::FunctionDecl> {
        return op.callee();
      },
      diagnostics);
}

std::optional<std::size_t> replace(Function& function, const Function& before,
                                   const Function& after,
                                   Diagnostics& diagnostics) {
  return detail::replace_expressions(function, before, after, diagnostics);
}

std::optional<std::size_t> replace(Module& module, const Function& before,
                                   const Function& after,
                                   Diagnostics& diagnostics) {
  try {
    Module candidate = module;
    std::size_t changed = 0;
    for (const Module::FunctionDecl& member : module.functions()) {
      const Function* source = member.body();
      if (source == nullptr) {
        continue;
      }
      Function rewritten = *source;
      const auto count =
          detail::replace_expressions(rewritten, before, after, diagnostics);
      if (!count) {
        return std::nullopt;
      }
      if (*count != 0U) {
        Function* destination = candidate.body(member);
        if (destination == nullptr) {
          diagnostics.report("Module lost function '" +
                             std::string(member.name()) + "'");
          return std::nullopt;
        }
        *destination = std::move(rewritten);
      }
      changed += *count;
    }
    if (changed != 0U) {
      module = std::move(candidate);
    }
    return changed;
  } catch (const std::exception& error) {
    diagnostics.report("Module expression replacement failed: " +
                       std::string(error.what()));
  } catch (...) {
    diagnostics.report(
        "Module expression replacement failed with an unknown exception");
  }
  return std::nullopt;
}

namespace {

struct OutlineTemplates {
  Function before;
  Function after;
  std::vector<Value> bindings;
};

std::optional<OutlineTemplates>
outline_templates(Compiler& compiler,
                  const Module::FunctionDecl& reference,
                  const std::vector<Value>& arguments, const Op& root,
                  Diagnostics& diagnostics) {
  if (reference.form() != Module::FunctionDecl::Form::Body) {
    diagnostics.report("outline reference '" +
                       reference.symbol().qualified_name() +
                       "' has no source body");
    return std::nullopt;
  }
  if (arguments.size() != reference.inputs().size()) {
    diagnostics.report("outline selector returned " +
                       std::to_string(arguments.size()) +
                       " arguments for '" +
                       reference.symbol().qualified_name() + "', expected " +
                       std::to_string(reference.inputs().size()));
    return std::nullopt;
  }
  const auto results = root.results();
  if (results.size() != 1U) {
    diagnostics.report("outline root must have exactly one result");
    return std::nullopt;
  }

  auto after = compiler.create_function();
  if (!after) {
    diagnostics.report("outline cannot create a call template");
    return std::nullopt;
  }
  auto edit = after->edit();
  std::vector<Value> call_arguments;
  std::vector<Value> bindings;
  call_arguments.reserve(arguments.size());
  bindings.reserve(arguments.size());
  for (const Value& argument : arguments) {
    if (argument.inline_function()) {
      diagnostics.report("outline arguments cannot contain an inline "
                         "function");
      return std::nullopt;
    }
    if (argument.known() || argument.referenced_function()) {
      call_arguments.push_back(argument);
    } else {
      call_arguments.push_back(edit.argument(argument.type()));
      bindings.push_back(argument);
    }
  }
  const Op call = edit.append(reference, std::move(call_arguments),
                              {results.front().type()});
  if (const auto location = root.location()) {
    edit.locate(call, *location);
  }
  edit.ret(after->entry(), {call.result(0)});
  if (!edit.commit(diagnostics)) {
    return std::nullopt;
  }
  auto before = compiler.materialize(call, diagnostics);
  if (!before) {
    diagnostics.report("outline cannot instantiate reference body for '" +
                       reference.symbol().qualified_name() + "'");
    return std::nullopt;
  }
  return OutlineTemplates{std::move(*before), std::move(*after),
                          std::move(bindings)};
}

}  // namespace

std::optional<std::size_t>
outline(Compiler& compiler, Function& function,
        Module::FunctionDecl reference, const OutlineArguments& select,
        Diagnostics& diagnostics, std::size_t max_expansions) {
  if (!select) {
    diagnostics.report("outline needs an argument selector");
    return std::nullopt;
  }
  try {
    Function candidate = function;
    const std::size_t limit = candidate.ops().size();
    if (limit == 0U) {
      return 0U;
    }
    std::size_t changed = 0U;
    for (std::size_t iteration = 0; iteration < limit; ++iteration) {
      std::optional<Op> selected;
      std::optional<std::vector<Value>> arguments;
      for (const Op& op : candidate.ops()) {
        auto current = select(candidate, op);
        if (current) {
          selected = op;
          arguments = std::move(current);
          break;
        }
      }
      if (!selected) {
        if (changed != 0U) {
          function = std::move(candidate);
        }
        return changed;
      }

      auto expressions = outline_templates(
          compiler, reference, *arguments, *selected, diagnostics);
      if (!expressions ||
          !equivalent(compiler, expressions->before, expressions->after,
                      diagnostics, max_expansions)) {
        return std::nullopt;
      }
      const std::vector<Value> roots{selected->result(0)};
      const auto matches = detail::match_expressions(
          candidate, expressions->before, diagnostics);
      const bool exact = matches && std::any_of(
          matches->begin(), matches->end(), [&](const auto& match) {
            return match.root == roots.front() &&
                   match.bindings == expressions->bindings;
          });
      if (!exact) {
        if (matches) {
          diagnostics.report("outline selector arguments do not match the "
                             "selected reference body");
        }
        return std::nullopt;
      }
      const auto count = detail::replace_expressions(
          candidate, expressions->before, expressions->after, diagnostics,
          roots);
      if (!count || *count != 1U) {
        if (count) {
          diagnostics.report("outline selector chose an expression that does "
                             "not match the reference body");
        }
        return std::nullopt;
      }
      ++changed;
    }
    diagnostics.report("outline did not converge after " +
                       std::to_string(limit) + " replacements");
  } catch (const std::exception& error) {
    diagnostics.report("outline failed: " + std::string(error.what()));
  } catch (...) {
    diagnostics.report("outline failed with an unknown exception");
  }
  return std::nullopt;
}

std::optional<std::size_t>
outline(Compiler& compiler, Module& module,
        Module::FunctionDecl reference, const OutlineArguments& select,
        Diagnostics& diagnostics, std::size_t max_expansions) {
  try {
    Module candidate = module;
    std::size_t changed = 0U;
    for (const Module::FunctionDecl& member : module.functions()) {
      const Function* source = member.body();
      if (source == nullptr) {
        continue;
      }
      Function rewritten = *source;
      const auto count = outline(compiler, rewritten, reference, select,
                                 diagnostics, max_expansions);
      if (!count) {
        return std::nullopt;
      }
      if (*count != 0U) {
        Function* destination = candidate.body(member);
        if (destination == nullptr) {
          diagnostics.report("Module lost function '" +
                             std::string(member.name()) + "'");
          return std::nullopt;
        }
        *destination = std::move(rewritten);
      }
      if (std::numeric_limits<std::size_t>::max() - changed < *count) {
        diagnostics.report("Module outline count overflowed");
        return std::nullopt;
      }
      changed += *count;
    }
    if (changed != 0U) {
      module = std::move(candidate);
    }
    return changed;
  } catch (const std::exception& error) {
    diagnostics.report("Module outline failed: " +
                       std::string(error.what()));
  } catch (...) {
    diagnostics.report("Module outline failed with an unknown exception");
  }
  return std::nullopt;
}

}  // namespace joggle
