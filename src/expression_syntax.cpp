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

constexpr int generic_argument_precedence = 41;

class ExpressionParser {
public:
  ExpressionParser(Lexer& lexer, Token& current, Diagnostics& diagnostics,
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
      const TokenKind first = current_.kind;
      const std::string symbol = take_operator();
      const int precedence = operator_precedence(symbol);
      if (first == TokenKind::Greater && precedence < minimum_precedence) {
        lexer_ = lookahead_lexer;
        current_ = lookahead_current;
        break;
      }
      const bool has_right_operand = starts_expression();
      if (!has_right_operand) {
        Module::Expression combined;
        combined.kind = Module::Expression::Kind::Postfix;
        combined.text = symbol;
        combined.arguments.push_back(std::move(result));
        result = std::move(combined);
        continue;
      }
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

  bool starts_expression() const {
    return is(TokenKind::Name) || is(TokenKind::Integer) ||
           is(TokenKind::Number) || is(TokenKind::String) ||
           is(TokenKind::At) || is(TokenKind::LeftParen) ||
           is(TokenKind::LeftBracket) || is(TokenKind::Plus) ||
           is(TokenKind::Minus) || is(TokenKind::Star) ||
           is(TokenKind::Slash) || is(TokenKind::Caret) ||
           is(TokenKind::Operator);
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
    return std::any_of(
        variables_.begin(), variables_.end(),
        [&](const auto& candidate) { return candidate.name == name; });
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
        // Parentheses are only required when the operand is not a primary.
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
      std::vector<Module::Expression> elements;
      if (!match(TokenKind::RightParen)) {
        do {
          elements.push_back(parse(0));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
      if (match(TokenKind::Arrow)) {
        std::vector<Module::Expression> results;
        if (match(TokenKind::LeftParen)) {
          if (!match(TokenKind::RightParen)) {
            do {
              results.push_back(parse(0));
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParen, "')'");
          }
        } else {
          results.push_back(parse(0));
        }
        result.kind = Kind::FunctionType;
        result.arguments.push_back(
            Module::Expression{Kind::List, {}, std::move(elements)});
        result.arguments.push_back(
            Module::Expression{Kind::List, {}, std::move(results)});
        return result;
      }
      if (elements.size() != 1U) {
        error("a parenthesized expression must contain one value");
        return result;
      }
      return std::move(elements.front());
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
    if (result.text.find('.') == std::string::npos && variable(result.text) &&
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
          // Comparison operators share angle brackets with generic syntax.
          // At this level `>` closes the argument list; parentheses opt an
          // argument back into the full expression grammar when needed.
          result.arguments.push_back(parse(generic_argument_precedence));
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

  static bool is_symbol(TokenKind kind) {
    return kind == TokenKind::Equal || kind == TokenKind::Less ||
           kind == TokenKind::Greater || kind == TokenKind::Plus ||
           kind == TokenKind::Minus || kind == TokenKind::Star ||
           kind == TokenKind::Slash || kind == TokenKind::Caret ||
           kind == TokenKind::Pipe || kind == TokenKind::Operator;
  }

  bool is_operator() const {
    if (!is_symbol(current_.kind)) {
      return false;
    }
    if (!is(TokenKind::Equal)) {
      return true;
    }
    Lexer lookahead = lexer_;
    const Token next = lookahead.take();
    return next.begin == current_.end && is_symbol(next.kind);
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
    } while (is_symbol(current_.kind));
    return result;
  }

  static int operator_precedence(std::string_view symbol) {
    if (symbol.empty()) {
      return 0;
    }
    switch (symbol.front()) {
    case '|':
      return 10;
    case '^':
      return 20;
    case '&':
      return 30;
    case '=':
    case '!':
    case '<':
    case '>':
      return 40;
    case '+':
    case '-':
      return 50;
    case '*':
    case '/':
    case '%':
      return 60;
    default:
      return 45;
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

Module::Expression
parse_expression(Lexer& lexer, Token& current, Diagnostics& diagnostics,
                 std::string_view source,
                 std::span<const Module::FunctionDecl::GenericDecl> variables,
                 int minimum_precedence) {
  return ExpressionParser(lexer, current, diagnostics, source, variables)
      .parse(minimum_precedence);
}

std::optional<CallableTypeView>
callable_type(const Module::Expression& expression) {
  using Kind = Module::Expression::Kind;
  if (expression.kind != Kind::FunctionType ||
      expression.arguments.size() != 2U ||
      expression.arguments[0].kind != Kind::List ||
      expression.arguments[1].kind != Kind::List) {
    return std::nullopt;
  }
  return CallableTypeView{expression.arguments[0].arguments,
                          expression.arguments[1].arguments};
}

namespace {

std::string escape_string(std::string_view value) {
  std::string result = "\"";
  for (const char character : value) {
    switch (character) {
    case '\\':
      result += "\\\\";
      break;
    case '"':
      result += "\\\"";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result.push_back(character);
      break;
    }
  }
  return result + '"';
}

int formatted_operator_precedence(std::string_view symbol) {
  if (symbol.empty()) {
    return 0;
  }
  switch (symbol.front()) {
  case '|':
    return 10;
  case '^':
    return 20;
  case '&':
    return 30;
  case '=':
  case '!':
  case '<':
  case '>':
    return 40;
  case '+':
  case '-':
    return 50;
  case '*':
  case '/':
  case '%':
    return 60;
  default:
    return 45;
  }
}

int expression_precedence(const Module::Expression& expression) {
  using Kind = Module::Expression::Kind;
  if (expression.kind == Kind::If) {
    return 5;
  }
  if (expression.kind == Kind::FunctionType) {
    return 1;
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
        result += format_expression(expression.arguments[index],
                                    generic_argument_precedence);
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
  } else if (expression.kind == Kind::FunctionType) {
    const auto write_types = [&](std::span<const Module::Expression> list) {
      std::string types;
      for (std::size_t index = 0; index < list.size(); ++index) {
        if (index != 0U) {
          types += ", ";
        }
        types += format_expression(list[index]);
      }
      return types;
    };
    if (const auto signature = callable_type(expression)) {
      result = "(" + write_types(signature->inputs) + ") -> ";
      if (signature->results.size() == 1U &&
          signature->results.front().kind != Kind::FunctionType) {
        result += format_expression(signature->results.front());
      } else {
        result += "(" + write_types(signature->results) + ")";
      }
    }
  } else if (expression.kind == Kind::Evaluate) {
    const auto& operand = expression.arguments.front();
    result = expression_precedence(operand) == 100
                 ? "@" + format_expression(operand)
                 : "@(" + format_expression(operand) + ')';
  } else if (expression.kind == Kind::If) {
    result = "if " + format_expression(expression.arguments[0]) + " { " +
             format_expression(expression.arguments[1]) + " } else { " +
             format_expression(expression.arguments[2]) + " }";
  } else if (expression.kind == Kind::Prefix) {
    result = expression.text +
             format_expression(expression.arguments.front(), precedence, true);
  } else if (expression.kind == Kind::Postfix) {
    result =
        format_expression(expression.arguments.front(), precedence, false) +
        expression.text;
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
