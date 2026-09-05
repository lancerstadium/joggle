#include "joggle/transform.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joggle/compiler.h"
#include "transform/clone.h"
#include "transform/nested.h"

namespace joggle {
namespace {

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

  detail::ValMap values;
  values.reserve(parameters.size() + candidate.body.ops().size());
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    values.emplace_back(parameters[index], supplied[index]);
  }

  auto returned = detail::clone_before(edit, candidate.body, candidate.call,
                                       values, diagnostics);
  if (!returned) {
    return false;
  }
  if (returned->size() != candidate.call.results().size()) {
    diagnostics.report("inline fn result count does not match its call");
    return false;
  }
  edit.replace(candidate.call, std::move(*returned));
  return true;
}

}  // namespace

namespace {

std::optional<std::size_t> inline_impl(Compiler& compiler, Fn& fn,
                                       Diag& diagnostics) {
  struct Nested {
    Val value;
    Fn body;
    std::size_t changed = 0;
  };
  std::vector<Nested> nested;
  std::size_t total = 0;
  for (const Val& value : detail::nested_values(fn)) {
    auto body = value.inline_fn();
    if (!body) {
      continue;
    }
    const auto changed = inline_impl(compiler, *body, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    if (*changed != 0U) {
      total += *changed;
      nested.push_back({value, std::move(*body), *changed});
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
    return total;
  }

  auto edit = fn.edit();
  for (const Candidate& selected : candidates) {
    if (!clone_call(edit, selected, diagnostics)) {
      return std::nullopt;
    }
  }
  if (!edit.commit(compiler, diagnostics)) {
    return std::nullopt;
  }
  return total + candidates.size();
}

}  // namespace

std::optional<std::size_t> inline_calls(Compiler& compiler, Fn& fn,
                                        Diag& diagnostics) {
  Fn candidate = fn;
  const auto changed = inline_impl(compiler, candidate, diagnostics);
  if (changed && *changed != 0U) {
    fn = std::move(candidate);
  }
  return changed;
}

std::optional<std::size_t> inline_calls(Compiler& compiler, Mod& mod,
                                        Diag& diagnostics) {
  Mod rewritten = mod;
  std::size_t total = 0;
  for (const Mod::FnDecl& original : mod.fns()) {
    if (original.body() == nullptr) {
      continue;
    }
    Fn body = *original.body();
    const auto changed = inline_calls(compiler, body, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    if (*changed == 0U) {
      continue;
    }
    const auto overloads = rewritten.overloads(original.name());
    const auto current = std::find_if(
        overloads.begin(), overloads.end(), [&](const Mod::FnDecl& candidate) {
          return candidate.symbol() == original.symbol();
        });
    if (current == overloads.end()) {
      diagnostics.report("cannot find the inlined fn in its Mod snapshot");
      return std::nullopt;
    }
    Fn* destination = rewritten.body(*current);
    if (destination == nullptr) {
      diagnostics.report("cannot publish an inlined fn body");
      return std::nullopt;
    }
    *destination = std::move(body);
    total += *changed;
  }
  if (total != 0U) {
    mod = std::move(rewritten);
  }
  return total;
}

}  // namespace joggle
