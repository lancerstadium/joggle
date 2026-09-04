#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "joggle/mod.h"

namespace joggle {

class Diag;

namespace detail {

class Lexer;
struct Token;

struct CallableTypeView {
  std::span<const Mod::Expr> inputs;
  std::span<const Mod::Expr> results;
};

std::optional<CallableTypeView> callable_type(const Mod::Expr& expression);

Mod::Expr
parse_expression(Lexer& lexer, Token& current, Diag& diagnostics,
                 std::string_view source,
                 std::span<const Mod::FnDecl::GenericDecl> variables = {},
                 int minimum_precedence = 0);

std::string format_expression(const Mod::Expr& expression,
                              int parent_precedence = 0,
                              bool right_operand = false);

}  // namespace detail
}  // namespace joggle
