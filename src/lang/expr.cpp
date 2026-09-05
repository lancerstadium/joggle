#include "lang/expr.h"

#include "sema/domain.h"
#include "lang/prelude.h"
#include "lang/lex.h"

#include "joggle/diag.h"

#include <algorithm>
#include <string>
#include <utility>

namespace joggle::detail {
namespace {

constexpr int generic_argument_precedence = 41;

class ExprParser {
public:
  ExprParser(Lexer& lexer, Token& current, Diag& diagnostics,
             std::string_view source,
             std::span<const Mod::FnDecl::GenericDecl> variables)
      : lexer_(lexer), current_(current), diagnostics_(diagnostics),
        source_(source), variables_(variables) {}

  Mod::Expr parse(int minimum_precedence) {
    Mod::Expr result;
    if (is_operator()) {
      result.kind = Mod::Expr::Kind::Prefix;
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
        Mod::Expr combined;
        combined.kind = Mod::Expr::Kind::Postfix;
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
      Mod::Expr combined;
      combined.kind = Mod::Expr::Kind::Infix;
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
               [&](const auto& candidate) { return candidate.name == name; }) ||
           std::find(lambda_variables_.begin(), lambda_variables_.end(),
                     name) != lambda_variables_.end();
  }

  Mod::Expr primary() {
    using Kind = Mod::Expr::Kind;
    Mod::Expr result;
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
      const bool starts_lambda = [&] {
        if (is(TokenKind::RightParen)) {
          Lexer lookahead = lexer_;
          const TokenKind next = lookahead.take().kind;
          return next == TokenKind::FatArrow || next == TokenKind::Arrow;
        }
        if (!is(TokenKind::Name)) {
          return false;
        }
        Lexer lookahead = lexer_;
        return lookahead.take().kind == TokenKind::Colon;
      }();
      if (starts_lambda) {
        result.kind = Kind::Lambda;
        std::vector<std::string> parameters;
        if (!match(TokenKind::RightParen)) {
          do {
            const std::string parameter = name("a lambda parameter");
            expect(TokenKind::Colon, "':' after a lambda parameter");
            if (std::find(parameters.begin(), parameters.end(), parameter) !=
                parameters.end()) {
              error("duplicate lambda parameter '" + parameter + "'");
            }
            result.labels.push_back(parameter);
            parameters.push_back(parameter);
            result.arguments.push_back(parse(0));
          } while (match(TokenKind::Comma));
          expect(TokenKind::RightParen, "')'");
        }
        if (match(TokenKind::Arrow)) {
          result.arguments.push_back(parse(0));
        }
        expect(TokenKind::FatArrow,
               "'=>' after lambda parameters or result type");
        const std::size_t previous = lambda_variables_.size();
        lambda_variables_.insert(lambda_variables_.end(), parameters.begin(),
                                 parameters.end());
        result.arguments.push_back(parse(0));
        lambda_variables_.resize(previous);
        return result;
      }
      std::vector<Mod::Expr> elements;
      if (!match(TokenKind::RightParen)) {
        do {
          elements.push_back(parse(0));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
      if (match(TokenKind::Arrow)) {
        std::vector<Mod::Expr> results;
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
        result.kind = Kind::FnType;
        result.arguments.push_back(
            Mod::Expr{Kind::List, {}, std::move(elements)});
        result.arguments.push_back(
            Mod::Expr{Kind::List, {}, std::move(results)});
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
         kernel_domain(Mod::Expr::reference(result.text)).has_value());
    if (result.text.find('.') == std::string::npos && !kernel_domain_name &&
        is_prelude_type(result.text)) {
      result.text = std::string(prelude_mod_name) + "." + result.text;
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
      const Loc::Pos end = current_.end;
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
    diagnostics_.report(std::move(message), Loc{std::string(source_),
                                                current_.begin, current_.end});
  }

  Lexer& lexer_;
  Token& current_;
  Diag& diagnostics_;
  std::string_view source_;
  std::span<const Mod::FnDecl::GenericDecl> variables_;
  std::vector<std::string> lambda_variables_;
};

}  // namespace

Mod::Expr parse_expression(Lexer& lexer, Token& current, Diag& diagnostics,
                           std::string_view source,
                           std::span<const Mod::FnDecl::GenericDecl> variables,
                           int minimum_precedence) {
  return ExprParser(lexer, current, diagnostics, source, variables)
      .parse(minimum_precedence);
}

std::optional<CallableTypeView> callable_type(const Mod::Expr& expression) {
  using Kind = Mod::Expr::Kind;
  if (expression.kind != Kind::FnType || expression.arguments.size() != 2U ||
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

int expression_precedence(const Mod::Expr& expression) {
  using Kind = Mod::Expr::Kind;
  if (expression.kind == Kind::If) {
    return 5;
  }
  if (expression.kind == Kind::FnType) {
    return 1;
  }
  if (expression.kind == Kind::Lambda) {
    return 2;
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

std::string format_expression(const Mod::Expr& expression,
                              int parent_precedence, bool right_operand) {
  using Kind = Mod::Expr::Kind;
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
  } else if (expression.kind == Kind::FnType) {
    const auto write_types = [&](std::span<const Mod::Expr> list) {
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
          signature->results.front().kind != Kind::FnType) {
        result += format_expression(signature->results.front());
      } else {
        result += "(" + write_types(signature->results) + ")";
      }
    }
  } else if (expression.kind == Kind::Lambda) {
    result = "(";
    const std::size_t parameter_count = expression.labels.size();
    const bool annotated =
        expression.arguments.size() == parameter_count + 2U;
    for (std::size_t index = 0; index < parameter_count; ++index) {
      if (index != 0U) {
        result += ", ";
      }
      result += expression.labels[index] + ": " +
                format_expression(expression.arguments[index]);
    }
    result += ')';
    if (annotated) {
      result += " -> " +
                format_expression(expression.arguments[parameter_count]);
    }
    result += " => ";
    if (!expression.arguments.empty()) {
      result += format_expression(expression.arguments.back());
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
