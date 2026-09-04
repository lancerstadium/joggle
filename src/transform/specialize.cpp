#include "joggle/compiler.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace joggle {
namespace {

class Specializer {
public:
  Specializer(Compiler& compiler, const Mod& input,
              const std::function<bool(const Mod::FnDecl&)>& boundary,
              Diag& diagnostics)
      : compiler_(compiler), input_(input), boundary_(boundary),
        diagnostics_(diagnostics), output_(input) {}

  std::optional<Mod> run() {
    for (const auto& member : input_.fns()) {
      const Fn* source = member.body();
      if (source == nullptr) {
        continue;
      }
      const auto replacements = plan(*source);
      Fn* target = output_.body(member);
      if (!replacements || target == nullptr ||
          !apply(*target, *replacements)) {
        return std::nullopt;
      }
    }
    if (!compiler_.verify(output_)) {
      diagnostics_.report("specialized Mod failed verification");
      return std::nullopt;
    }
    return std::move(output_);
  }

private:
  struct Replacement {
    std::string name;
    std::string signature;
  };

  using Plan = std::vector<std::optional<Replacement>>;

  std::optional<Mod::FnDecl> declaration(const Replacement& replacement) const {
    for (const auto& candidate : output_.overloads(replacement.name)) {
      if (candidate.signature() == replacement.signature) {
        return candidate;
      }
    }
    diagnostics_.report("specialization lost generated Fn '" +
                        replacement.name + "'");
    return std::nullopt;
  }

  std::optional<Replacement> specialize(const Op& call) {
    if (boundary_(call.callee())) {
      return Replacement{};
    }

    const auto body = compiler_.materialize(call, diagnostics_);
    if (!body) {
      diagnostics_.report(
          "call '" + call.callee().symbol().qualified_name() +
          "' has neither a source body nor an accepted implementation");
      return std::nullopt;
    }

    std::string key = call.callee().symbol().stable_name();
    key += '\n';
    key += call.callee().signature();
    key += '\n';
    key += format(*body, call.callee().name());
    if (const auto known = generated_.find(key); known != generated_.end()) {
      return known->second;
    }
    if (!active_.insert(key).second) {
      diagnostics_.report("recursive source specialization at '" +
                          call.callee().symbol().qualified_name() + "'");
      return std::nullopt;
    }

    const auto replacements = plan(*body);
    if (!replacements) {
      active_.erase(key);
      return std::nullopt;
    }

    std::string name;
    do {
      name = "specialized_" + std::to_string(next_name_++) + "_" +
             std::string(call.callee().name());
    } while (!output_.overloads(name).empty());
    if (!output_.insert(name, *body, diagnostics_)) {
      active_.erase(key);
      return std::nullopt;
    }

    const auto inserted = output_.fn(name);
    Fn* generated = inserted ? output_.body(*inserted) : nullptr;
    if (!inserted || generated == nullptr ||
        !apply(*generated, *replacements)) {
      active_.erase(key);
      return std::nullopt;
    }

    Replacement replacement{name, inserted->signature()};
    active_.erase(key);
    generated_.emplace(std::move(key), replacement);
    return replacement;
  }

  std::optional<Plan> plan(const Fn& fn) {
    Plan result;
    result.reserve(fn.ops().size());
    for (const Op& call : fn.ops()) {
      auto replacement = specialize(call);
      if (!replacement) {
        return std::nullopt;
      }
      if (replacement->name.empty()) {
        result.push_back(std::nullopt);
      } else {
        result.push_back(std::move(*replacement));
      }
    }
    return result;
  }

  bool apply(Fn& fn, const Plan& replacements) {
    const auto calls = fn.ops();
    if (calls.size() != replacements.size()) {
      diagnostics_.report("specialization plan does not match its Fn");
      return false;
    }
    if (std::none_of(
            replacements.begin(), replacements.end(),
            [](const auto& replacement) { return replacement.has_value(); })) {
      return true;
    }

    auto edit = fn.edit();
    for (std::size_t index = 0; index < replacements.size(); ++index) {
      if (!replacements[index]) {
        continue;
      }
      const auto callee = declaration(*replacements[index]);
      if (!callee) {
        return false;
      }
      std::vector<Type> results;
      results.reserve(calls[index].results().size());
      for (const Val& result : calls[index].results()) {
        results.push_back(result.type());
      }
      const Op linked =
          edit.insert(calls[index], *callee, calls[index].operands(), results);
      edit.replace(calls[index], linked.results());
    }
    return edit.commit(diagnostics_);
  }

  Compiler& compiler_;
  const Mod& input_;
  const std::function<bool(const Mod::FnDecl&)>& boundary_;
  Diag& diagnostics_;
  Mod output_;
  std::set<std::string> active_;
  std::unordered_map<std::string, Replacement> generated_;
  std::size_t next_name_ = 0;
};

}  // namespace

std::optional<Mod>
Compiler::specialize(const Mod& mod,
                     const std::function<bool(const Mod::FnDecl&)>& boundary,
                     Diag& diagnostics) {
  if (!boundary) {
    diagnostics.report("specialization needs an accepted-call boundary");
    return std::nullopt;
  }
  return Specializer(*this, mod, boundary, diagnostics).run();
}

}  // namespace joggle
