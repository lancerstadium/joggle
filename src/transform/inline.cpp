#include "joggle/transform.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joggle/compiler.h"

namespace joggle {
namespace {

using ValueMap = std::vector<std::pair<Val, Val>>;

std::optional<Val> mapped(const ValueMap& values, const Val& source) {
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
                               ValueMap& values, Diag& diagnostics) {
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

struct Candidate {
  Op call;
  Fn body;
  std::vector<Val> captures;
};

std::optional<Candidate> candidate(Compiler& compiler, const Op& call,
                                   Diag& diagnostics) {
  const Val callee = call.callee();
  if (const auto body = callee.inline_fn()) {
    if (body->blks().size() != 1U) {
      return std::nullopt;
    }
    return Candidate{call, *body, callee.captures()};
  }
  const auto declaration = callee.referenced_fn();
  if (!declaration || declaration->form() != Mod::FnDecl::Form::Body) {
    return std::nullopt;
  }
  auto body = compiler.materialize(call, diagnostics);
  if (!body) {
    return std::nullopt;
  }
  if (body->blks().size() != 1U) {
    return std::nullopt;
  }
  return Candidate{call, std::move(*body), {}};
}

bool clone_call(Fn::Edit& edit, const Candidate& candidate, Diag& diagnostics) {
  if (!candidate.body.entry().valid()) {
    diagnostics.report("cannot inline an invalid fn body");
    return false;
  }

  std::vector<Val> supplied = candidate.call.arguments();
  supplied.insert(supplied.end(), candidate.captures.begin(),
                  candidate.captures.end());
  const std::vector<Val> parameters = candidate.body.arguments();
  if (parameters.size() != supplied.size()) {
    diagnostics.report("inline fn argument count does not match its call");
    return false;
  }

  ValueMap values;
  values.reserve(parameters.size() + candidate.body.ops().size());
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    values.emplace_back(parameters[index], supplied[index]);
  }

  for (const Op& source : candidate.body.ops()) {
    const auto callee = clone_value(edit, source.callee(), values, diagnostics);
    if (!callee) {
      return false;
    }
    std::vector<Val> arguments;
    arguments.reserve(source.arguments().size());
    for (const Val& argument : source.arguments()) {
      const auto value = clone_value(edit, argument, values, diagnostics);
      if (!value) {
        return false;
      }
      arguments.push_back(*value);
    }
    std::vector<Type> result_types;
    for (const Val& result : source.results()) {
      result_types.push_back(result.type());
    }
    Op cloned = edit.call_before(candidate.call, *callee, std::move(arguments),
                                 std::move(result_types));
    if (const auto location = source.location()) {
      edit.locate(cloned, *location);
    }
    const std::vector<Val> source_results = source.results();
    const std::vector<Val> cloned_results = cloned.results();
    for (std::size_t index = 0; index < source_results.size(); ++index) {
      values.emplace_back(source_results[index], cloned_results[index]);
    }
  }

  const Term term = candidate.body.entry().terminator();
  if (term.kind() != Term::Kind::Return) {
    diagnostics.report("single-block inline fn does not return");
    return false;
  }
  std::vector<Val> returned;
  returned.reserve(term.returned().size());
  for (const Val& value : term.returned()) {
    const auto result = clone_value(edit, value, values, diagnostics);
    if (!result) {
      return false;
    }
    returned.push_back(*result);
  }
  if (returned.size() != candidate.call.results().size()) {
    diagnostics.report("inline fn result count does not match its call");
    return false;
  }
  edit.replace(candidate.call, std::move(returned));
  return true;
}

}  // namespace

std::optional<std::size_t> inline_calls(Compiler& compiler, Fn& fn,
                                        Diag& diagnostics) {
  std::vector<Candidate> candidates;
  for (const Op& call : fn.ops()) {
    const std::size_t issue_count = diagnostics.issues().size();
    auto selected = candidate(compiler, call, diagnostics);
    if (!selected) {
      if (diagnostics.issues().size() != issue_count) {
        return std::nullopt;
      }
      continue;
    }
    candidates.push_back(std::move(*selected));
  }
  if (candidates.empty()) {
    return std::size_t{0};
  }

  auto edit = fn.edit();
  for (const Candidate& selected : candidates) {
    if (!clone_call(edit, selected, diagnostics)) {
      return std::nullopt;
    }
  }
  if (!edit.commit(diagnostics)) {
    return std::nullopt;
  }
  return candidates.size();
}

}  // namespace joggle
