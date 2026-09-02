#include "expression_syntax.h"

#include "domain.h"
#include "prelude.h"
#include "syntax_lexer.h"

#include "joggle/diagnostic.h"

#include <algorithm>
#include <string>
#include <utility>

namespace joggle::detail {
namespace {

class ExpressionParser {
public:
  ExpressionParser(
      Lexer& lexer, Token& current, Diagnostics& diagnostics,
      std::string_view source,
      std::span<const Module::FunctionDecl::GenericDecl> variables)
      : lexer_(lexer), current_(current), diagnostics_(diagnostics),
        source_(source), variables_(variables) {}

  Module::Expression parse(int minimum_precedence) {
    Module::Expression result;
    if (is_operator()) {
      result.kind = Module::Expression::Kind::Prefix;
      result.text = take_operator();
      result.arguments.push_back(parse(70));
    } else {
      result = primary();
    }

    while (is_operator()) {
      Lexer lookahead_lexer = lexer_;
      Token lookahead_current = current_;
      const std::string symbol = take_operator();
      const bool has_right_operand =
          !is(TokenKind::Semicolon) && !is(TokenKind::Comma) &&
          !is(TokenKind::RightParen) && !is(TokenKind::RightBracket) &&
          !is(TokenKind::RightBrace) && !is(TokenKind::Greater);
      if (!has_right_operand) {
        Module::Expression combined;
        combined.kind = Module::Expression::Kind::Postfix;
        combined.text = symbol;
        combined.arguments.push_back(std::move(result));
        result = std::move(combined);
        continue;
      }
      const int precedence = operator_precedence(symbol);
      if (precedence < minimum_precedence) {
        lexer_ = lookahead_lexer;
        current_ = lookahead_current;
        break;
      }
      Module::Expression combined;
      combined.kind = Module::Expression::Kind::Infix;
      combined.text = symbol;
      combined.arguments.push_back(std::move(result));
      combined.arguments.push_back(parse(precedence + 1));
      result = std::move(combined);
    }
    return result;
  }

private:
  bool is(TokenKind kind) const { return current_.kind == kind; }
  bool is_name(std::string_view value) const {
    return is(TokenKind::Name) && current_.text == value;
  }

  void advance() {
    current_ = lexer_.take();
    if (current_.kind == TokenKind::Invalid) {
      error(current_.text);
    }
  }

  bool match(TokenKind kind) {
    if (!is(kind)) {
      return false;
    }
    advance();
    return true;
  }

  bool match_name(std::string_view value) {
    if (!is_name(value)) {
      return false;
    }
    advance();
    return true;
  }

  void expect(TokenKind kind, std::string_view description) {
    if (!match(kind)) {
      error("expected " + std::string(description));
    }
  }

  void expect_name(std::string_view value) {
    if (!match_name(value)) {
      error("expected '" + std::string(value) + "'");
    }
  }

  std::string name(std::string_view description) {
    if (!is(TokenKind::Name)) {
      error("expected " + std::string(description));
      return {};
    }
    std::string result = current_.text;
    advance();
    return result;
  }

  std::string reference(std::string_view description) {
    std::string result = name(description);
    if (match(TokenKind::Dot)) {
      result += "." + name(description);
    }
    return result;
  }

  bool variable(std::string_view name) const {
    return std::any_of(variables_.begin(), variables_.end(),
                       [&](const auto& candidate) {
                         return candidate.name == name;
                       });
  }

  Module::Expression primary() {
    using Kind = Module::Expression::Kind;
    Module::Expression result;
    if (match(TokenKind::At)) {
      result.kind = Kind::Evaluate;
      if (match(TokenKind::LeftParen)) {
        result.arguments.push_back(parse(0));
        expect(TokenKind::RightParen, "')'");
      } else {
        // Convenient input only. The formatter always emits `@(expression)`.
        result.arguments.push_back(primary());
      }
      return result;
    }
    if (match_name("if")) {
      result.kind = Kind::If;
      result.arguments.push_back(parse(0));
      expect(TokenKind::LeftBrace, "'{' after if condition");
      result.arguments.push_back(parse(0));
      expect(TokenKind::RightBrace, "'}' after if branch");
      expect_name("else");
      expect(TokenKind::LeftBrace, "'{' after else");
      result.arguments.push_back(parse(0));
      expect(TokenKind::RightBrace, "'}' after else branch");
      return result;
    }
    if (match(TokenKind::LeftParen)) {
      result = parse(0);
      expect(TokenKind::RightParen, "')'");
      return result;
    }
    if (is(TokenKind::Integer) || is(TokenKind::Number)) {
      result.kind = Kind::Number;
      result.text = current_.text;
      advance();
      return result;
    }
    if (is(TokenKind::String)) {
      result.kind = Kind::String;
      result.text = current_.text;
      advance();
      return result;
    }
    if (is_name("true") || is_name("false")) {
      result.kind = Kind::Boolean;
      result.text = current_.text;
      advance();
      return result;
    }
    if (match(TokenKind::LeftBracket)) {
      result.kind = Kind::List;
      if (!match(TokenKind::RightBracket)) {
        do {
          result.arguments.push_back(parse(0));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightBracket, "']'");
      }
      return result;
    }

    result.text = reference("an expression");
    if (result.text.find('.') == std::string::npos &&
        variable(result.text) && !is(TokenKind::Less) &&
        !is(TokenKind::LeftParen)) {
      result.kind = Kind::Variable;
      return result;
    }
    const bool kernel_domain_name =
        result.text.find('.') == std::string::npos &&
        (result.text == "list" ||
         kernel_domain(Module::Expression::reference(result.text)).has_value());
    if (result.text.find('.') == std::string::npos && !kernel_domain_name &&
        is_prelude_type(result.text)) {
      result.text = std::string(prelude_module_name) + "." + result.text;
    }
    if (match(TokenKind::Less)) {
      result.kind = Kind::Reference;
      if (!match(TokenKind::Greater)) {
        do {
          result.arguments.push_back(parse(0));
        } while (match(TokenKind::Comma));
        expect(TokenKind::Greater, "'>'");
      }
    } else if (match(TokenKind::LeftParen)) {
      result.kind = Kind::Call;
      bool saw_label = false;
      if (!match(TokenKind::RightParen)) {
        do {
          std::string label;
          if (is(TokenKind::Name)) {
            Lexer lookahead = lexer_;
            const Token next = lookahead.take();
            if (next.kind == TokenKind::Colon ||
                next.kind == TokenKind::Equal) {
              label = current_.text;
              advance();
              if (!match(TokenKind::Colon)) {
                expect(TokenKind::Equal, "':'");
              }
            }
          }
          if (label.empty() && saw_label) {
            error("a positional argument cannot follow a named argument");
          } else if (!label.empty() &&
                     std::find(result.labels.begin(), result.labels.end(),
                               label) != result.labels.end()) {
            error("duplicate named argument '" + label + "'");
          }
          saw_label = saw_label || !label.empty();
          result.arguments.push_back(parse(0));
          result.labels.push_back(std::move(label));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
    } else {
      result.kind = Kind::Reference;
    }
    return result;
  }

  bool is_operator() const {
    return is(TokenKind::Plus) || is(TokenKind::Minus) ||
           is(TokenKind::Star) || is(TokenKind::Slash) ||
           is(TokenKind::Caret) || is(TokenKind::Operator);
  }

  std::string take_operator() {
    std::string result;
    do {
      result += current_.text;
      const SourcePosition end = current_.end;
      advance();
      if (current_.begin != end) {
        break;
      }
    } while (is_operator());
    return result;
  }

  static int operator_precedence(std::string_view symbol) {
    if (symbol.empty()) {
      return 0;
    }
    switch (symbol.front()) {
    case '|': return 10;
    case '^': return 20;
    case '&': return 30;
    case '=':
    case '!':
    case '<':
    case '>': return 40;
    case '+':
    case '-': return 50;
    case '*':
    case '/':
    case '%': return 60;
    default: return 45;
    }
  }

  void error(std::string message) {
    diagnostics_.report(
        std::move(message),
        SourceRange{std::string(source_), current_.begin, current_.end});
  }

  Lexer& lexer_;
  Token& current_;
  Diagnostics& diagnostics_;
  std::string_view source_;
  std::span<const Module::FunctionDecl::GenericDecl> variables_;
};

}  // namespace

Module::Expression parse_expression(
    Lexer& lexer, Token& current, Diagnostics& diagnostics,
    std::string_view source,
    std::span<const Module::FunctionDecl::GenericDecl> variables,
    int minimum_precedence) {
  return ExpressionParser(lexer, current, diagnostics, source, variables)
      .parse(minimum_precedence);
}

namespace {

std::string escape_string(std::string_view value) {
  std::string result = "\"";
  for (const char character : value) {
    switch (character) {
    case '\\': result += "\\\\"; break;
    case '"': result += "\\\""; break;
    case '\n': result += "\\n"; break;
    case '\r': result += "\\r"; break;
    case '\t': result += "\\t"; break;
    default: result.push_back(character); break;
    }
  }
  return result + '"';
}

int formatted_operator_precedence(std::string_view symbol) {
  if (symbol.empty()) {
    return 0;
  }
  switch (symbol.front()) {
  case '|': return 10;
  case '^': return 20;
  case '&': return 30;
  case '=':
  case '!':
  case '<':
  case '>': return 40;
  case '+':
  case '-': return 50;
  case '*':
  case '/':
  case '%': return 60;
  default: return 45;
  }
}

int expression_precedence(const Module::Expression& expression) {
  using Kind = Module::Expression::Kind;
  if (expression.kind == Kind::If) {
    return 5;
  }
  if (expression.kind == Kind::Infix) {
    return formatted_operator_precedence(expression.text);
  }
  if (expression.kind == Kind::Prefix) {
    return 70;
  }
  if (expression.kind == Kind::Postfix) {
    return 80;
  }
  if (expression.kind == Kind::Evaluate) {
    return 90;
  }
  return 100;
}

}  // namespace

std::string format_expression(const Module::Expression& expression,
                              int parent_precedence, bool right_operand) {
  using Kind = Module::Expression::Kind;
  const int precedence = expression_precedence(expression);
  std::string result;
  if (expression.kind == Kind::String) {
    result = escape_string(expression.text);
  } else if (expression.kind == Kind::Number ||
             expression.kind == Kind::Boolean ||
             expression.kind == Kind::Variable) {
    result = expression.text;
  } else if (expression.kind == Kind::List) {
    result = "[";
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      if (index != 0U) {
        result += ", ";
      }
      result += format_expression(expression.arguments[index]);
    }
    result += ']';
  } else if (expression.kind == Kind::Reference) {
    result = display_type_name(expression.text);
    if (!expression.arguments.empty()) {
      result += '<';
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        if (index != 0U) {
          result += ", ";
        }
        result += format_expression(expression.arguments[index]);
      }
      result += '>';
    }
  } else if (expression.kind == Kind::Call) {
    result = expression.text + '(';
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      if (index != 0U) {
        result += ", ";
      }
      if (index < expression.labels.size() &&
          !expression.labels[index].empty()) {
        result += expression.labels[index] + ": ";
      }
      result += format_expression(expression.arguments[index]);
    }
    result += ')';
  } else if (expression.kind == Kind::Evaluate) {
    result = "@(" + format_expression(expression.arguments.front()) + ')';
  } else if (expression.kind == Kind::If) {
    result = "if " + format_expression(expression.arguments[0]) + " { " +
             format_expression(expression.arguments[1]) + " } else { " +
             format_expression(expression.arguments[2]) + " }";
  } else if (expression.kind == Kind::Prefix) {
    result = expression.text +
             format_expression(expression.arguments.front(), precedence,
                               true);
  } else if (expression.kind == Kind::Postfix) {
    result = format_expression(expression.arguments.front(), precedence,
                               false) + expression.text;
  } else {
    result = format_expression(expression.arguments[0], precedence, false) +
             " " + expression.text + " " +
             format_expression(expression.arguments[1], precedence, true);
  }
  if (precedence < parent_precedence ||
      (right_operand && precedence == parent_precedence && precedence < 100)) {
    return '(' + result + ')';
  }
  return result;
}

}  // namespace joggle::detail
