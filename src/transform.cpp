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
    std::vector<Value> pending{returned.front()};
    while (!pending.empty()) {
      const Value value = pending.back();
      pending.pop_back();
      if (value.known() || value.referenced_function() ||
          std::find(holes.begin(), holes.end(), value) != holes.end()) {
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
    return true;
  } catch (const std::exception& error) {
    return reject("validation failed: " + std::string(error.what()));
  } catch (...) {
    return reject("validation failed with an unknown exception");
  }
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

}  // namespace joggle
