#include "transform/clone.h"

#include <algorithm>
#include <optional>
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

}  // namespace joggle::detail
