#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "joggle/compiler.h"

namespace joggle::detail {

struct CallCandidate {
  Mod::FnDecl fn;
  // For every source argument, the corresponding declaration parameter.
  std::vector<std::size_t> parameters;
};

std::vector<Mod::FnDecl> visible_fns(const Compiler& compiler,
                                     std::string_view owner,
                                     std::string_view reference);
std::vector<Mod::FnDecl> visible_fns(std::span<const Mod> mods,
                                     std::string_view owner,
                                     std::string_view reference);
std::vector<Mod::FnDecl> visible_operators(const Compiler& compiler,
                                           std::string_view owner,
                                           std::string_view symbol,
                                           Mod::FnDecl::Fixity fixity);
std::vector<Mod::FnDecl> visible_operators(std::span<const Mod> mods,
                                           std::string_view owner,
                                           std::string_view symbol,
                                           Mod::FnDecl::Fixity fixity);
std::vector<Mod::FnDecl>
operator_candidates(const Compiler& compiler, std::string_view owner,
                    std::string_view symbol, Mod::FnDecl::Fixity fixity,
                    std::size_t arity, const Mod::Expr& result_domain);

std::optional<CallCandidate> call_candidate(const Mod::FnDecl& fn,
                                            const Mod::Expr& expression);

}  // namespace joggle::detail
