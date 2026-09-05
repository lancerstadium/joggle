#include "sema/call.h"

#include "lang/prelude.h"

#include <algorithm>

namespace joggle::detail {
namespace {

bool same_signature(const Mod::FnDecl& left, const Mod::FnDecl& right) {
  if (left.generics().size() != right.generics().size() ||
      left.inputs().size() != right.inputs().size() ||
      left.results().size() != right.results().size()) {
    return false;
  }
  const bool same_inputs = std::equal(
      left.inputs().begin(), left.inputs().end(), right.inputs().begin(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.domain == rhs.domain && lhs.variadic == rhs.variadic;
      });
  const bool same_results =
      std::equal(left.results().begin(), left.results().end(),
                 right.results().begin(), [](const auto& lhs, const auto& rhs) {
                   return lhs.domain == rhs.domain;
                 });
  return same_inputs && same_results;
}

void append_unshadowed(std::vector<Mod::FnDecl>& destination,
                       std::span<const Mod::FnDecl> candidates) {
  for (const auto& candidate : candidates) {
    const bool shadowed = std::any_of(
        destination.begin(), destination.end(), [&](const auto& visible) {
          return same_signature(visible, candidate);
        });
    if (!shadowed) {
      destination.push_back(candidate);
    }
  }
}

template <typename Lookup>
std::vector<Mod::FnDecl> find_visible_fns(Lookup&& lookup,
                                          std::string_view owner,
                                          std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string mod_name(owner);
  std::string_view local = reference;
  if (dot != std::string_view::npos) {
    const std::string_view prefix = reference.substr(0U, dot);
    local = reference.substr(dot + 1U);
    if (prefix == prelude_mod_name) {
      mod_name = std::string(prelude_mod_name);
    }
    if (prefix != owner && prefix != prelude_mod_name) {
      const auto scope = lookup(owner);
      const auto imported =
          scope ? std::find_if(scope->imports().begin(), scope->imports().end(),
                               [&](const Mod::Import& import) {
                                 return import.prefix() == prefix;
                               })
                : std::span<const Mod::Import>::iterator{};
      if (!scope || imported == scope->imports().end()) {
        return {};
      }
      mod_name = imported->name;
    }
  }
  const auto mod = lookup(mod_name);
  std::vector<Mod::FnDecl> result =
      mod ? mod->overloads(local) : std::vector<Mod::FnDecl>{};
  if (mod_name != owner) {
    result.erase(
        std::remove_if(result.begin(), result.end(),
                       [](const Mod::FnDecl& fn) { return !fn.exported(); }),
        result.end());
  }
  if (dot == std::string_view::npos && mod_name != prelude_mod_name &&
      result.empty()) {
    if (const auto prelude = lookup(prelude_mod_name)) {
      result = prelude->overloads(local);
      result.erase(
          std::remove_if(result.begin(), result.end(),
                         [](const Mod::FnDecl& fn) { return !fn.exported(); }),
          result.end());
    }
  }
  return result;
}

template <typename Lookup>
std::vector<Mod::FnDecl>
find_visible_operators(Lookup&& lookup, std::string_view owner,
                       std::string_view symbol, Mod::FnDecl::Fixity fixity) {
  std::vector<Mod::FnDecl> result;
  const auto scope = lookup(owner);
  if (!scope) {
    return result;
  }
  const auto append = [&](const Mod& mod, bool imported) {
    for (const auto& fn : mod.fns()) {
      if ((!imported || fn.exported()) && fn.name() == symbol &&
          fn.operator_fixity() == fixity) {
        result.push_back(fn);
      }
    }
  };
  append(*scope, false);
  for (const auto& import : scope->imports()) {
    if (const auto mod = lookup(import.name)) {
      append(*mod, true);
    }
  }
  if (scope->name() != prelude_mod_name) {
    if (const auto prelude = lookup(prelude_mod_name)) {
      std::vector<Mod::FnDecl> ambient;
      for (const auto& fn : prelude->fns()) {
        if (fn.exported() && fn.name() == symbol &&
            fn.operator_fixity() == fixity) {
          ambient.push_back(fn);
        }
      }
      append_unshadowed(result, ambient);
    }
  }
  return result;
}

}  // namespace

std::optional<Mod> visible_mod(const Compiler& compiler, std::string_view owner,
                               std::string_view reference) {
  if (reference == owner) {
    return compiler.mod(owner);
  }
  const auto scope = compiler.mod(owner);
  if (!scope) {
    return std::nullopt;
  }
  const auto imported = std::find_if(
      scope->imports().begin(), scope->imports().end(),
      [&](const Mod::Import& import) { return import.prefix() == reference; });
  return imported == scope->imports().end() ? std::optional<Mod>{}
                                            : compiler.mod(imported->name);
}

std::vector<Mod::FnDecl> visible_fns(const Compiler& compiler,
                                     std::string_view owner,
                                     std::string_view reference) {
  return find_visible_fns(
      [&](std::string_view name) { return compiler.mod(name); }, owner,
      reference);
}

std::vector<Mod::FnDecl> visible_fns(std::span<const Mod> mods,
                                     std::string_view owner,
                                     std::string_view reference) {
  return find_visible_fns(
      [&](std::string_view name) -> std::optional<Mod> {
        const auto found =
            std::find_if(mods.begin(), mods.end(),
                         [&](const auto& item) { return item.name() == name; });
        return found == mods.end() ? std::optional<Mod>{}
                                   : std::optional<Mod>{*found};
      },
      owner, reference);
}

std::vector<Mod::FnDecl> visible_operators(const Compiler& compiler,
                                           std::string_view owner,
                                           std::string_view symbol,
                                           Mod::FnDecl::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) { return compiler.mod(name); }, owner, symbol,
      fixity);
}

std::vector<Mod::FnDecl> visible_operators(std::span<const Mod> mods,
                                           std::string_view owner,
                                           std::string_view symbol,
                                           Mod::FnDecl::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) -> std::optional<Mod> {
        const auto found =
            std::find_if(mods.begin(), mods.end(),
                         [&](const auto& item) { return item.name() == name; });
        return found == mods.end() ? std::optional<Mod>{}
                                   : std::optional<Mod>{*found};
      },
      owner, symbol, fixity);
}

std::vector<Mod::FnDecl>
operator_candidates(const Compiler& compiler, std::string_view owner,
                    std::string_view symbol, Mod::FnDecl::Fixity fixity,
                    std::size_t arity, const Mod::Expr& result_domain) {
  auto result = visible_operators(compiler, owner, symbol, fixity);
  result.erase(std::remove_if(result.begin(), result.end(),
                              [&](const auto& candidate) {
                                return candidate.inputs().size() != arity ||
                                       candidate.results().size() != 1U ||
                                       candidate.results().front().domain !=
                                           result_domain;
                              }),
               result.end());
  return result;
}

std::optional<CallCandidate> call_candidate(const Mod::FnDecl& fn,
                                            const Mod::Expr& expression) {
  const auto parameters = fn.inputs();
  CallCandidate result{fn, {}};
  result.parameters.reserve(expression.arguments.size());
  std::vector<bool> supplied(parameters.size(), false);
  std::size_t positional = 0;
  for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
    const std::string_view label = index < expression.labels.size()
                                       ? expression.labels[index]
                                       : std::string_view{};
    std::size_t target = parameters.size();
    if (!label.empty()) {
      const auto found = std::find_if(parameters.begin(), parameters.end(),
                                      [&](const Mod::ParamDecl& parameter) {
                                        return parameter.name == label;
                                      });
      if (found != parameters.end()) {
        target =
            static_cast<std::size_t>(std::distance(parameters.begin(), found));
      }
    } else {
      while (positional < parameters.size() && supplied[positional] &&
             !parameters[positional].variadic) {
        ++positional;
      }
      if (positional < parameters.size()) {
        target = positional;
        if (!parameters[target].variadic) {
          ++positional;
        }
      }
    }
    if (target == parameters.size() ||
        (supplied[target] && !parameters[target].variadic)) {
      return std::nullopt;
    }
    supplied[target] = true;
    result.parameters.push_back(target);
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!supplied[index] && !parameters[index].variadic &&
        !parameters[index].default_value) {
      return std::nullopt;
    }
  }
  return result;
}

}  // namespace joggle::detail
