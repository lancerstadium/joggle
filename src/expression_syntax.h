#pragma once

#include <span>
#include <string>
#include <string_view>

#include "joggle/module.h"

namespace joggle {

class Diagnostics;

namespace detail {

class Lexer;
struct Token;

Module::Expression parse_expression(
    Lexer& lexer, Token& current, Diagnostics& diagnostics,
    std::string_view source,
    std::span<const Module::FunctionDecl::GenericDecl> variables = {},
    int minimum_precedence = 0);

std::string format_expression(const Module::Expression& expression,
                              int parent_precedence = 0,
                              bool right_operand = false);

}  // namespace detail
}  // namespace joggle
