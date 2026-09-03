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

std::vector<Module::FunctionDecl>
visible_functions(const Compiler& compiler, std::string_view owner,
                  std::string_view reference);
std::vector<Module::FunctionDecl>
visible_operators(const Compiler& compiler, std::string_view owner,
                  std::string_view symbol,
                  Module::FunctionDecl::Fixity fixity);

std::optional<CallCandidate>
call_candidate(const Module::FunctionDecl& function,
               const Module::Expression& expression);

bool is_bootstrap_call(std::string_view name);
bool is_bootstrap_operator(std::string_view symbol);

}  // namespace joggle::detail
