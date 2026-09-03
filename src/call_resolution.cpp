#include "call_resolution.h"

#include <algorithm>
#include <array>

namespace joggle::detail {
namespace {

template <typename Lookup>
std::vector<Module::FunctionDecl> find_visible_functions(
    Lookup&& lookup, std::string_view owner, std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string module_name(owner);
  std::string_view local = reference;
  if (dot != std::string_view::npos) {
    const std::string_view prefix = reference.substr(0U, dot);
    local = reference.substr(dot + 1U);
    if (prefix != owner) {
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
  return module ? module->overloads(local)
                : std::vector<Module::FunctionDecl>{};
}

template <typename Lookup>
std::vector<Module::FunctionDecl> find_visible_operators(
    Lookup&& lookup, std::string_view owner, std::string_view symbol,
    Module::FunctionDecl::Fixity fixity) {
  std::vector<Module::FunctionDecl> result;
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
  return result;
}

}  // namespace

std::vector<Module::FunctionDecl>
visible_functions(const Compiler& compiler, std::string_view owner,
                  std::string_view reference) {
  return find_visible_functions(
      [&](std::string_view name) { return compiler.module(name); }, owner,
      reference);
}

std::vector<Module::FunctionDecl>
visible_functions(std::span<const Module> modules, std::string_view owner,
                  std::string_view reference) {
  return find_visible_functions(
      [&](std::string_view name) -> std::optional<Module> {
        const auto found =
            std::find_if(modules.begin(), modules.end(), [&](const auto& item) {
              return item.name() == name;
            });
        return found == modules.end() ? std::optional<Module>{}
                                      : std::optional<Module>{*found};
      },
      owner, reference);
}

std::vector<Module::FunctionDecl>
visible_operators(const Compiler& compiler, std::string_view owner,
                  std::string_view symbol,
                  Module::FunctionDecl::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) { return compiler.module(name); }, owner,
      symbol, fixity);
}

std::vector<Module::FunctionDecl>
visible_operators(std::span<const Module> modules, std::string_view owner,
                  std::string_view symbol,
                  Module::FunctionDecl::Fixity fixity) {
  return find_visible_operators(
      [&](std::string_view name) -> std::optional<Module> {
        const auto found =
            std::find_if(modules.begin(), modules.end(), [&](const auto& item) {
              return item.name() == name;
            });
        return found == modules.end() ? std::optional<Module>{}
                                      : std::optional<Module>{*found};
      },
      owner, symbol, fixity);
}

std::vector<Module::FunctionDecl> operator_candidates(
    const Compiler& compiler, std::string_view owner, std::string_view symbol,
    Module::FunctionDecl::Fixity fixity, std::size_t arity,
    const Module::Expression& result_domain) {
  auto result = visible_operators(compiler, owner, symbol, fixity);
  result.erase(
      std::remove_if(result.begin(), result.end(), [&](const auto& candidate) {
        return candidate.inputs().size() != arity ||
               candidate.results().size() != 1U ||
               candidate.results().front().domain != result_domain;
      }),
      result.end());
  return result;
}

std::optional<CallCandidate>
call_candidate(const Module::FunctionDecl& function,
               const Module::Expression& expression) {
  const auto parameters = function.inputs();
  if (std::any_of(parameters.begin(), parameters.end(),
                  [](const Module::ParameterDecl& parameter) {
                    return parameter.variadic;
                  })) {
    return std::nullopt;
  }
  CallCandidate result{function, {}};
  result.parameters.reserve(expression.arguments.size());
  std::vector<bool> supplied(parameters.size(), false);
  std::size_t positional = 0;
  for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
    const std::string_view label =
        index < expression.labels.size() ? expression.labels[index]
                                         : std::string_view{};
    std::size_t target = parameters.size();
    if (!label.empty()) {
      const auto found = std::find_if(
          parameters.begin(), parameters.end(),
          [&](const Module::ParameterDecl& parameter) {
            return parameter.name == label;
          });
      if (found != parameters.end()) {
        target = static_cast<std::size_t>(
            std::distance(parameters.begin(), found));
      }
    } else {
      while (positional < parameters.size() && supplied[positional]) {
        ++positional;
      }
      if (positional < parameters.size()) {
        target = positional++;
      }
    }
    if (target == parameters.size() || supplied[target]) {
      return std::nullopt;
    }
    supplied[target] = true;
    result.parameters.push_back(target);
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!supplied[index] && !parameters[index].default_value) {
      return std::nullopt;
    }
  }
  return result;
}

bool is_bootstrap_call(std::string_view name) {
  return name == "ceildiv" || name == "min" || name == "max";
}

bool is_bootstrap_operator(std::string_view symbol) {
  constexpr std::array<std::string_view, 5> values{
      "+", "-", "*", "/", "//"};
  return std::find(values.begin(), values.end(), symbol) != values.end();
}

}  // namespace joggle::detail
