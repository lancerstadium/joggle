#include "call_resolution.h"

#include "prelude.h"

#include <algorithm>

namespace joggle::detail {
namespace {

bool same_signature(const Module::Function& left,
                    const Module::Function& right) {
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

void append_unshadowed(std::vector<Module::Function>& destination,
                       std::span<const Module::Function> candidates) {
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
std::vector<Module::Function>
find_visible_functions(Lookup&& lookup, std::string_view owner,
                       std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string module_name(owner);
  std::string_view local = reference;
  if (dot != std::string_view::npos) {
    const std::string_view prefix = reference.substr(0U, dot);
    local = reference.substr(dot + 1U);
    if (prefix == prelude_module_name) {
      module_name = std::string(prelude_module_name);
    }
    if (prefix != owner && prefix != prelude_module_name) {
      const auto scope = lookup(owner);
      const auto imported =
          scope ? std::find_if(scope->imports().begin(), scope->imports().end(),
                               [&](const Module::Import& import) {
                                 return import.prefix() == prefix;
                               })
                : std::span<const Module::Import>::iterator{};
      if (!scope || imported == scope->imports().end()) {
        return {};
      }
      module_name = imported->name;
    }
  }
  const auto module = lookup(module_name);
  std::vector<Module::Function> result =
      module ? module->overloads(local) : std::vector<Module::Function>{};
  if (dot == std::string_view::npos && module_name != prelude_module_name &&
      result.empty()) {
    if (const auto prelude = lookup(prelude_module_name)) {
      result = prelude->overloads(local);
    }
  }
  return result;
}

template <typename Lookup>
std::vector<Module::Function>
find_visible_operators(Lookup&& lookup, std::string_view owner,
                       std::string_view symbol,
                       Module::Function::Fixity fixity) {
  std::vector<Module::Function> result;
  const auto scope = lookup(owner);
  if (!scope) {
    return result;
  }
  const auto append = [&](const Module& module) {
    for (const auto& function : module.functions()) {
      if (function.operator_symbol() == symbol &&
          function.operator_fixity() == fixity) {
        result.push_back(function);
      }
    }
  };
  append(*scope);
  for (const auto& import : scope->imports()) {
    if (const auto module = lookup(import.name)) {
      append(*module);
    }
  }
  if (scope->name() != prelude_module_name) {
    if (const auto prelude = lookup(prelude_module_name)) {
      std::vector<Module::Function> ambient;
      for (const auto& function : prelude->functions()) {
        if (function.operator_symbol() == symbol &&
            function.operator_fixity() == fixity) {
          ambient.push_back(function);
        }
      }
      append_unshadowed(result, ambient);
    }
  }
  return result;
}

}  // namespace

std::vector<Module::Function>
visible_functions(const Compiler& compiler, std::string_view owner,
                  std::string_view reference) {
  return find_visible_functions(
      [&](std::string_view name) { return compiler.module(name); }, owner,
      reference);
}

std::vector<Module::Function>
visible_functions(std::span<const Module> modules, std::string_view owner,
                  std::string_view reference) {
  return find_visible_functions(
      [&](std::string_view name) -> std::optional<Module> {
        const auto found =
            std::find_if(modules.begin(), modules.end(),
                         [&](const auto& item) { return item.name() == name; });
        return found == modules.end() ? std::optional<Module>{}
                                      : std::optional<Module>{*found};
      },
      owner, reference);
}

std::vector<Module::Function>
visible_operators(const Compiler& compiler, std::string_view owner,
                  std::string_view symbol,
                  Module::Function::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) { return compiler.module(name); }, owner,
      symbol, fixity);
}

std::vector<Module::Function>
visible_operators(std::span<const Module> modules, std::string_view owner,
                  std::string_view symbol,
                  Module::Function::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) -> std::optional<Module> {
        const auto found =
            std::find_if(modules.begin(), modules.end(),
                         [&](const auto& item) { return item.name() == name; });
        return found == modules.end() ? std::optional<Module>{}
                                      : std::optional<Module>{*found};
      },
      owner, symbol, fixity);
}

std::vector<Module::Function>
operator_candidates(const Compiler& compiler, std::string_view owner,
                    std::string_view symbol,
                    Module::Function::Fixity fixity, std::size_t arity,
                    const Module::Expression& result_domain) {
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

std::optional<CallCandidate>
call_candidate(const Module::Function& function,
               const Module::Expression& expression) {
  const auto parameters = function.inputs();
  CallCandidate result{function, {}};
  result.parameters.reserve(expression.arguments.size());
  std::vector<bool> supplied(parameters.size(), false);
  std::size_t positional = 0;
  for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
    const std::string_view label = index < expression.labels.size()
                                       ? expression.labels[index]
                                       : std::string_view{};
    std::size_t target = parameters.size();
    if (!label.empty()) {
      const auto found =
          std::find_if(parameters.begin(), parameters.end(),
                       [&](const Module::ParameterDecl& parameter) {
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
