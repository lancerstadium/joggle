#include "transform/clone.h"

#include "ir/fn.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace joggle::detail {
namespace {

std::optional<Val> mapped(const ValMap& values, const Val& source) {
  if (source.known()) {
    return source;
  }
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto& item) { return item.first == source; });
  return found == values.end() ? std::nullopt
                               : std::optional<Val>{found->second};
}

std::optional<Val> clone_value(Fn::Edit& edit, const Val& source,
                               ValMap& values, Diag& diagnostics) {
  if (const auto existing = mapped(values, source)) {
    return existing;
  }

  if (const auto declaration = source.referenced_fn()) {
    std::vector<std::pair<std::string, Val>> bindings;
    bindings.reserve(source.bindings().size());
    for (const auto& [name, binding] : source.bindings()) {
      const auto value = clone_value(edit, binding, values, diagnostics);
      if (!value) {
        diagnostics.report("cannot clone fn binding '" + name + "'");
        return std::nullopt;
      }
      bindings.emplace_back(name, *value);
    }
    Val result =
        edit.reference(*declaration, source.type(), std::move(bindings));
    values.emplace_back(source, result);
    return result;
  }

  if (const auto body = source.inline_fn()) {
    std::vector<Val> captures;
    captures.reserve(source.captures().size());
    for (const Val& capture : source.captures()) {
      const auto value = clone_value(edit, capture, values, diagnostics);
      if (!value) {
        diagnostics.report("cannot clone an inline fn capture");
        return std::nullopt;
      }
      captures.push_back(*value);
    }
    Val result = edit.callable(*body, source.type(), std::move(captures));
    values.emplace_back(source, result);
    return result;
  }

  diagnostics.report("cannot clone a value before its definition");
  return std::nullopt;
}

std::optional<Val> clone_expression(Fn::Edit& edit, const Val& source,
                                    Op before, ValMap& values,
                                    Diag& diagnostics,
                                    const std::optional<Loc>& location) {
  if (const auto existing = mapped(values, source)) {
    return existing;
  }
  if (source.referenced_fn() || source.inline_fn()) {
    std::vector<Val> dependencies;
    for (const auto& [name, binding] : source.bindings()) {
      static_cast<void>(name);
      dependencies.push_back(binding);
    }
    const auto captures = source.captures();
    dependencies.insert(dependencies.end(), captures.begin(), captures.end());
    for (const Val& dependency : dependencies) {
      if (!clone_expression(edit, dependency, before, values, diagnostics,
                            location)) {
        return std::nullopt;
      }
    }
    return clone_value(edit, source, values, diagnostics);
  }
  const auto operation = source.defining_op();
  if (!operation) {
    diagnostics.report("cannot clone an unbound expression value");
    return std::nullopt;
  }
  const auto callee = clone_expression(edit, operation->callee(), before,
                                       values, diagnostics, location);
  if (!callee) {
    return std::nullopt;
  }
  std::vector<Val> arguments;
  arguments.reserve(operation->arguments().size());
  for (const Val& argument : operation->arguments()) {
    const auto value =
        clone_expression(edit, argument, before, values, diagnostics, location);
    if (!value) {
      return std::nullopt;
    }
    arguments.push_back(*value);
  }
  std::vector<Type> result_types;
  for (const Val& result : operation->results()) {
    result_types.push_back(result.type());
  }
  Op cloned = edit.call_before(before, *callee, std::move(arguments),
                               std::move(result_types));
  if (location) {
    edit.locate(cloned, *location);
  } else if (const auto source_location = operation->location()) {
    edit.locate(cloned, *source_location);
  }
  const auto original_results = operation->results();
  const auto cloned_results = cloned.results();
  for (std::size_t index = 0; index < original_results.size(); ++index) {
    values.emplace_back(original_results[index], cloned_results[index]);
  }
  return mapped(values, source);
}

}  // namespace

std::optional<std::vector<Val>> clone_before(Fn::Edit& edit, const Fn& source,
                                             Op before, ValMap& values,
                                             Diag& diagnostics,
                                             std::optional<Loc> location) {
  if (source.blks().size() != 1U || !source.entry().valid()) {
    diagnostics.report("a cloned fn must have one entry block");
    return std::nullopt;
  }
  for (const Op& operation : source.ops()) {
    const auto callee =
        clone_value(edit, operation.callee(), values, diagnostics);
    if (!callee) {
      return std::nullopt;
    }
    std::vector<Val> arguments;
    arguments.reserve(operation.arguments().size());
    for (const Val& argument : operation.arguments()) {
      const auto value = clone_value(edit, argument, values, diagnostics);
      if (!value) {
        return std::nullopt;
      }
      arguments.push_back(*value);
    }
    std::vector<Type> result_types;
    for (const Val& result : operation.results()) {
      result_types.push_back(result.type());
    }
    Op cloned = edit.call_before(before, *callee, std::move(arguments),
                                 std::move(result_types));
    if (location) {
      edit.locate(cloned, *location);
    } else if (const auto source_location = operation.location()) {
      edit.locate(cloned, *source_location);
    }
    const auto source_results = operation.results();
    const auto cloned_results = cloned.results();
    for (std::size_t index = 0; index < source_results.size(); ++index) {
      values.emplace_back(source_results[index], cloned_results[index]);
    }
  }

  const Term term = source.entry().terminator();
  if (term.kind() != Term::Kind::Return) {
    diagnostics.report("a cloned single-block fn must return");
    return std::nullopt;
  }
  std::vector<Val> returned;
  returned.reserve(term.returned().size());
  for (const Val& value : term.returned()) {
    const auto result = clone_value(edit, value, values, diagnostics);
    if (!result) {
      return std::nullopt;
    }
    returned.push_back(*result);
  }
  return returned;
}

bool inline_cfg(Fn::Edit& edit, const Fn& source, Op before, ValMap& values,
                Diag& diagnostics, std::optional<Loc> location) {
  if (!source.entry().valid()) {
    diagnostics.report("cannot inline an invalid fn body");
    return false;
  }

  try {
    const Blk continuation = FnAccess::split(edit, before);
    const Blk destination_entry = before.parent();
    std::vector<std::pair<Blk, Blk>> blocks{
        {source.entry(), destination_entry}};
    for (const Blk& source_block : source.blks()) {
      if (source_block.is_entry()) {
        if (!source_block.arguments().empty()) {
          diagnostics.report(
              "a source entry block cannot have block arguments");
          return false;
        }
        continue;
      }
      std::vector<Type> types;
      for (const Val& argument : source_block.arguments()) {
        types.push_back(argument.type());
      }
      const Blk destination = edit.blk(std::move(types));
      blocks.emplace_back(source_block, destination);
      const auto source_arguments = source_block.arguments();
      const auto destination_arguments = destination.arguments();
      for (std::size_t index = 0; index < source_arguments.size(); ++index) {
        values.emplace_back(source_arguments[index],
                            destination_arguments[index]);
      }
    }
    const auto destination = [&](Blk source_block) -> std::optional<Blk> {
      const auto found =
          std::find_if(blocks.begin(), blocks.end(), [&](const auto& item) {
            return item.first == source_block;
          });
      return found == blocks.end() ? std::nullopt
                                   : std::optional<Blk>{found->second};
    };

    for (const Blk& source_block : source.blks()) {
      const auto target = destination(source_block);
      if (!target) {
        diagnostics.report("cannot map an inlined source block");
        return false;
      }
      for (const Op& operation : source_block.ops()) {
        const auto callee =
            clone_value(edit, operation.callee(), values, diagnostics);
        if (!callee) {
          return false;
        }
        std::vector<Val> arguments;
        for (const Val& argument : operation.arguments()) {
          const auto replacement =
              clone_value(edit, argument, values, diagnostics);
          if (!replacement) {
            return false;
          }
          arguments.push_back(*replacement);
        }
        std::vector<Type> result_types;
        for (const Val& result : operation.results()) {
          result_types.push_back(result.type());
        }
        const Op cloned = edit.call(*target, *callee, std::move(arguments),
                                    std::move(result_types));
        if (location) {
          edit.locate(cloned, *location);
        } else if (const auto source_location = operation.location()) {
          edit.locate(cloned, *source_location);
        }
        const auto original_results = operation.results();
        const auto cloned_results = cloned.results();
        for (std::size_t index = 0; index < original_results.size(); ++index) {
          values.emplace_back(original_results[index], cloned_results[index]);
        }
      }

      const Term term = source_block.terminator();
      const auto map_value = [&](const Val& value) -> std::optional<Val> {
        return clone_value(edit, value, values, diagnostics);
      };
      const auto map_values = [&](std::span<const Val> source_values)
          -> std::optional<std::vector<Val>> {
        std::vector<Val> result;
        result.reserve(source_values.size());
        for (const Val& value : source_values) {
          const auto replacement = map_value(value);
          if (!replacement) {
            return std::nullopt;
          }
          result.push_back(*replacement);
        }
        return result;
      };
      if (term.kind() == Term::Kind::Return) {
        const auto returned = map_values(term.returned());
        if (!returned) {
          return false;
        }
        edit.jump(*target, continuation, *returned);
      } else if (term.kind() == Term::Kind::Jump) {
        const auto successor = destination(term.successor(0U));
        const auto arguments = map_values(term.arguments(0U));
        if (!successor || !arguments) {
          diagnostics.report("cannot map an inlined jump");
          return false;
        }
        edit.jump(*target, *successor, *arguments);
      } else {
        const auto condition = term.condition();
        const auto mapped_condition =
            condition ? map_value(*condition) : std::optional<Val>{};
        const auto true_target = destination(term.successor(0U));
        const auto false_target = destination(term.successor(1U));
        const auto true_arguments = map_values(term.arguments(0U));
        const auto false_arguments = map_values(term.arguments(1U));
        if (!mapped_condition || !true_target || !false_target ||
            !true_arguments || !false_arguments) {
          diagnostics.report("cannot map an inlined branch");
          return false;
        }
        edit.branch(*target, *mapped_condition, *true_target, *true_arguments,
                    *false_target, *false_arguments);
      }
    }

    const auto results = before.results();
    const auto replacements = continuation.arguments();
    if (results.size() != replacements.size()) {
      diagnostics.report("inline fn result count does not match its call");
      return false;
    }
    edit.replace(before, replacements);
    return true;
  } catch (const std::exception& error) {
    diagnostics.report("cannot inline CFG: " + std::string(error.what()));
    return false;
  }
}

std::optional<Val> clone_before(Fn::Edit& edit, const Val& source, Op before,
                                ValMap& values, Diag& diagnostics,
                                std::optional<Loc> location) {
  return clone_expression(edit, source, before, values, diagnostics, location);
}

}  // namespace joggle::detail
