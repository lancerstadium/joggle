#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "joggle/compiler.h"

namespace joggle::detail {

struct CallCandidate {
  Module::FunctionDecl function;
  // For every source argument, the corresponding declaration parameter.
  std::vector<std::size_t> parameters;
};

std::vector<Module::FunctionDecl> visible_functions(const Compiler& compiler,
                                                    std::string_view owner,
                                                    std::string_view reference);
std::vector<Module::FunctionDecl>
visible_functions(std::span<const Module> modules, std::string_view owner,
                  std::string_view reference);
std::vector<Module::FunctionDecl>
visible_operators(const Compiler& compiler, std::string_view owner,
                  std::string_view symbol, Module::FunctionDecl::Fixity fixity);
std::vector<Module::FunctionDecl>
visible_operators(std::span<const Module> modules, std::string_view owner,
                  std::string_view symbol, Module::FunctionDecl::Fixity fixity);
std::vector<Module::FunctionDecl>
operator_candidates(const Compiler& compiler, std::string_view owner,
                    std::string_view symbol,
                    Module::FunctionDecl::Fixity fixity, std::size_t arity,
                    const Module::Expression& result_domain);

std::optional<CallCandidate>
call_candidate(const Module::FunctionDecl& function,
               const Module::Expression& expression);

}  // namespace joggle::detail
