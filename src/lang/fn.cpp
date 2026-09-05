#include "lang/fn.h"

#include "ir/mod.h"
#include "lang/expr.h"
#include "lang/prelude.h"
#include "ir/fn.h"
#include "lang/lex.h"
#include "ir/type.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace joggle {
namespace {

using detail::ParamVal;
using TokenKind = detail::TokenKind;
using Token = detail::Token;
using Lexer = detail::Lexer;

class SyntaxParser {
public:
  SyntaxParser(Lexer& lexer, Token& current, Diag& diagnostics,
               std::string source,
               std::span<const Mod::FnDecl::GenericDecl> variables)
      : lexer_(lexer), diagnostics_(diagnostics), source_(std::move(source)),
        initial_diagnostics_(diagnostics.size()), current_(current),
        variables_(variables.begin(), variables.end()) {
    for (const auto& variable : variables_) {
      locals_.insert(variable.name);
    }
  }

  std::optional<detail::FnBody> parse() {
    const Loc::Pos begin = current_.begin;
    detail::FnBody body;
    body.source = source_;
    expect(TokenKind::LeftBrace, "'{'");
    if (looks_like_block_header()) {
      while (looks_like_block_header() && ok()) {
        body.blocks.push_back(parse_block());
      }
      if (!is(TokenKind::RightBrace)) {
        error("expected a block header or '}'");
      }
    } else {
      detail::BlkSyntax entry;
      entry.name = "entry";
      entry.range.begin = current_.begin;
      while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
        if (is_name("jump") || is_name("branch")) {
          error("jump and branch require explicit Blks");
          break;
        }
        entry.statements.push_back(parse_statement());
      }
      entry.range.end = previous_end_;
      body.blocks.push_back(std::move(entry));
    }
    if (!is(TokenKind::RightBrace)) {
      error("expected '}'");
    } else {
      body.range = {begin, current_.end};
      advance();
    }
    if (!ok()) {
      return std::nullopt;
    }
    if (!detail::verify_fn_body(body, diagnostics_)) {
      return std::nullopt;
    }
    return body;
  }

private:
  bool ok() const { return diagnostics_.size() == initial_diagnostics_; }
  bool is(TokenKind kind) const { return current_.kind == kind; }
  bool is_name(std::string_view value) const {
    return is(TokenKind::Name) && current_.text == value;
  }

  void advance() {
    previous_end_ = current_.end;
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

  std::optional<std::string> name(std::string_view description) {
    if (!is(TokenKind::Name)) {
      error("expected " + std::string(description));
      return std::nullopt;
    }
    std::string result = current_.text;
    advance();
    return result;
  }

  std::optional<std::string> reference(std::string_view description) {
    auto first = name(description);
    if (!first || !match(TokenKind::Dot)) {
      return first;
    }
    auto second = name(description);
    return second ? std::optional<std::string>{*first + "." + *second}
                  : std::nullopt;
  }

  std::optional<std::string> local_name() { return name("a local value name"); }

  detail::ExprSyntax expression() {
    const Loc::Pos begin = current_.begin;
    auto value = detail::parse_expression(lexer_, current_, diagnostics_,
                                          source_, variables_);
    return {std::move(value), {begin, current_.begin}};
  }

  bool is_terminator() const {
    return is_name("return") || is_name("jump") || is_name("branch");
  }

  bool looks_like_block_header() const {
    if (!is(TokenKind::Name)) {
      return false;
    }
    Lexer lookahead = lexer_;
    Token token = lookahead.take();
    if (token.kind != TokenKind::LeftParen) {
      return false;
    }
    std::size_t depth = 1U;
    while (depth != 0U) {
      token = lookahead.take();
      if (token.kind == TokenKind::End || token.kind == TokenKind::Invalid) {
        return false;
      }
      if (token.kind == TokenKind::LeftParen) {
        ++depth;
      } else if (token.kind == TokenKind::RightParen) {
        --depth;
      }
    }
    return lookahead.take().kind == TokenKind::Colon;
  }

  detail::SuccessorSyntax parse_successor() {
    detail::SuccessorSyntax successor;
    const Loc::Pos begin = current_.begin;
    if (auto target = name("a successor block name")) {
      successor.target = std::move(*target);
    }
    expect(TokenKind::LeftParen, "'('");
    if (!match(TokenKind::RightParen)) {
      do {
        successor.arguments.push_back(expression());
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
    successor.range = {begin, previous_end_};
    return successor;
  }

  detail::TermSyntax parse_terminator() {
    detail::TermSyntax terminator;
    const Loc::Pos begin = current_.begin;
    if (match_name("return")) {
      terminator.kind = detail::TermSyntax::Kind::Return;
      if (!is(TokenKind::Semicolon)) {
        do {
          terminator.values.push_back(expression());
        } while (match(TokenKind::Comma));
      }
    } else if (match_name("jump")) {
      terminator.kind = detail::TermSyntax::Kind::Jump;
      terminator.successors.push_back(parse_successor());
    } else if (match_name("branch")) {
      terminator.kind = detail::TermSyntax::Kind::Branch;
      terminator.condition = expression();
      expect(TokenKind::Comma, "','");
      terminator.successors.push_back(parse_successor());
      expect(TokenKind::Comma, "','");
      terminator.successors.push_back(parse_successor());
    } else {
      error("expected return, jump, or branch");
    }
    expect(TokenKind::Semicolon, "';'");
    terminator.range = {begin, previous_end_};
    return terminator;
  }

  detail::BlkSyntax parse_block() {
    detail::BlkSyntax block;
    const Loc::Pos begin = current_.begin;
    if (auto block_name = name("a block name")) {
      block.name = std::move(*block_name);
    }
    expect(TokenKind::LeftParen, "'('");
    if (!match(TokenKind::RightParen)) {
      do {
        detail::BlkArgSyntax argument;
        const Loc::Pos argument_begin = current_.begin;
        if (auto argument_name = local_name()) {
          argument.name = std::move(*argument_name);
        }
        expect(TokenKind::Colon, "':'");
        argument.type = expression();
        argument.range = {argument_begin, previous_end_};
        variables_.push_back({argument.name, Mod::Expr::reference("type")});
        locals_.insert(argument.name);
        block.arguments.push_back(std::move(argument));
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
    expect(TokenKind::Colon, "':'");
    while (!is_terminator() && !looks_like_block_header() &&
           !is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
      block.statements.push_back(parse_statement());
    }
    if (!is_terminator()) {
      error("block '" + block.name + "' has no terminator");
    } else {
      block.terminator = parse_terminator();
    }
    block.range = {begin, previous_end_};
    return block;
  }

  bool starts_binding() const {
    if (!is(TokenKind::Name)) {
      return false;
    }
    Lexer lookahead = lexer_;
    const Token next = lookahead.take();
    return next.kind == TokenKind::Colon || next.kind == TokenKind::Comma ||
           next.kind == TokenKind::Equal;
  }

  detail::StatementSyntax parse_statement() {
    detail::StatementSyntax statement;
    const Loc::Pos begin = current_.begin;
    if (is_name("break") || is_name("continue")) {
      const bool is_break = is_name("break");
      advance();
      statement.kind = is_break ? detail::StatementSyntax::Kind::Break
                                : detail::StatementSyntax::Kind::Continue;
      if (loop_depth_ == 0U) {
        error(std::string(is_break ? "break" : "continue") +
              " is only valid inside a structured loop");
      }
      expect(TokenKind::Semicolon, "';'");
      statement.range = {begin, previous_end_};
      return statement;
    }
    if (match_name("return")) {
      statement.kind = detail::StatementSyntax::Kind::Return;
      if (!is(TokenKind::Semicolon)) {
        do {
          statement.values.push_back(expression());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::Semicolon, "';'");
      statement.range = {begin, previous_end_};
      return statement;
    }
    if (match_name("if")) {
      statement.kind = detail::StatementSyntax::Kind::If;
      statement.expression = expression();
      const auto outer_variables = variables_;
      const auto outer_locals = locals_;
      const auto parse_arm = [&](std::vector<detail::StatementSyntax>& arm,
                                 std::string_view role) {
        expect(TokenKind::LeftBrace, "'{' after " + std::string(role));
        while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
          if (is_name("jump") || is_name("branch")) {
            error("jump and branch are only available in explicit Blks");
            break;
          }
          arm.push_back(parse_statement());
        }
        expect(TokenKind::RightBrace, "'}' after " + std::string(role));
      };
      parse_arm(statement.body, "if condition");
      variables_ = outer_variables;
      locals_ = outer_locals;
      if (match_name("else")) {
        statement.has_else = true;
        parse_arm(statement.otherwise, "else");
        variables_ = outer_variables;
        locals_ = outer_locals;
      }
      statement.range = {begin, previous_end_};
      return statement;
    }
    if (match_name("while")) {
      statement.kind = detail::StatementSyntax::Kind::While;
      statement.expression = expression();
      expect(TokenKind::LeftBrace, "'{' after while condition");
      const auto outer_variables = variables_;
      const auto outer_locals = locals_;
      ++loop_depth_;
      while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
        if (is_name("jump") || is_name("branch")) {
          error("jump and branch are only available in explicit Blks");
          break;
        }
        statement.body.push_back(parse_statement());
      }
      --loop_depth_;
      expect(TokenKind::RightBrace, "'}' after while body");
      variables_ = outer_variables;
      locals_ = outer_locals;
      statement.range = {begin, previous_end_};
      return statement;
    }
    if (match_name("for")) {
      statement.kind = detail::StatementSyntax::Kind::For;
      const Loc::Pos iterator_begin = current_.begin;
      if (auto iterator = local_name()) {
        statement.iterator =
            detail::BindingSyntax{std::move(*iterator),
                                  std::nullopt,
                                  {iterator_begin, previous_end_},
                                  false};
      }
      if (statement.iterator && match(TokenKind::Colon)) {
        statement.iterator->type = expression();
        statement.iterator->range.end = previous_end_;
      }
      expect_name("in");
      statement.expression = expression();
      expect(TokenKind::LeftBrace, "'{' after for iterable");
      const auto outer_variables = variables_;
      const auto outer_locals = locals_;
      if (statement.iterator) {
        locals_.insert(statement.iterator->name);
        variables_.push_back(
            {statement.iterator->name, statement.iterator->type
                                           ? statement.iterator->type->value
                                           : Mod::Expr::reference("type")});
      }
      ++loop_depth_;
      while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
        if (is_name("jump") || is_name("branch")) {
          error("jump and branch are only available in explicit Blks");
          break;
        }
        statement.body.push_back(parse_statement());
      }
      --loop_depth_;
      expect(TokenKind::RightBrace, "'}' after for body");
      variables_ = outer_variables;
      locals_ = outer_locals;
      statement.range = {begin, previous_end_};
      return statement;
    }
    if (starts_binding()) {
      auto add_binding = [&](std::string name, Loc::Pos binding_begin) {
        detail::BindingSyntax binding;
        binding.name = std::move(name);
        binding.rebind = locals_.contains(binding.name);
        if (match(TokenKind::Colon)) {
          binding.type = expression();
        }
        binding.range = {binding_begin, previous_end_};
        if (!binding.rebind) {
          locals_.insert(binding.name);
          variables_.push_back({binding.name, Mod::Expr::reference("type")});
        }
        statement.bindings.push_back(std::move(binding));
      };
      const Loc::Pos first_begin = current_.begin;
      if (auto first = local_name()) {
        add_binding(std::move(*first), first_begin);
      }
      while (match(TokenKind::Comma)) {
        const Loc::Pos binding_begin = current_.begin;
        if (auto binding = local_name()) {
          add_binding(std::move(*binding), binding_begin);
        }
      }
      expect(TokenKind::Equal, "'='");
    }
    statement.expression = expression();
    if (is(TokenKind::LeftBrace)) {
      error("a call cannot own a nested body; pass a named fn as an "
            "ordinary argument");
    }
    expect(TokenKind::Semicolon, "';'");
    statement.range = {begin, previous_end_};
    return statement;
  }

  void error(std::string message) {
    diagnostics_.report(std::move(message),
                        Loc{source_, current_.begin, current_.end});
  }

  Lexer& lexer_;
  Diag& diagnostics_;
  std::string source_;
  std::size_t initial_diagnostics_ = 0;
  Token& current_;
  Loc::Pos previous_end_;
  std::vector<Mod::FnDecl::GenericDecl> variables_;
  std::unordered_set<std::string> locals_;
  std::size_t loop_depth_ = 0;
};

std::string escape(std::string_view value) {
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

class SyntaxWriter {
public:
  SyntaxWriter(const detail::FnSyntax& fn, std::size_t indent)
      : fn_(&fn), body_(&fn.body), indent_(indent) {}

  SyntaxWriter(const detail::FnBody& body, std::size_t indent)
      : body_(&body), indent_(indent) {}

  std::string write() {
    if (fn_ == nullptr) {
      write_body();
      return output_.str();
    }
    output_ << spaces(indent_) << "fn " << fn_->name;
    std::size_t signature_width = indent_ * 2U + 3U + fn_->name.size() + 2U;
    for (std::size_t index = 0; index < fn_->arguments.size(); ++index) {
      signature_width += fn_->arguments[index].name.size() + 3U +
                         value_width(fn_->arguments[index].type) +
                         (index == 0U ? 0U : 2U);
    }
    if (!fn_->result_types.empty()) {
      signature_width += 4U;
      if (fn_->result_types.size() > 1U) {
        signature_width += 2U;
      }
      for (std::size_t index = 0; index < fn_->result_types.size(); ++index) {
        signature_width +=
            value_width(fn_->result_types[index]) + (index == 0U ? 0U : 2U);
      }
    }
    const bool multiline_arguments =
        !fn_->arguments.empty() && signature_width > line_limit;
    if (multiline_arguments) {
      output_ << "(\n";
      for (std::size_t index = 0; index < fn_->arguments.size(); ++index) {
        output_ << spaces(indent_ + 1U) << fn_->arguments[index].name << ": ";
        write_value(fn_->arguments[index].type);
        if (index + 1U != fn_->arguments.size()) {
          output_ << ',';
        }
        output_ << '\n';
      }
      output_ << spaces(indent_) << ')';
    } else {
      write_arguments(fn_->arguments);
    }
    if (!fn_->result_types.empty()) {
      output_ << " -> ";
      write_type_list(fn_->result_types);
    }
    output_ << ' ';
    write_body();
    return output_.str();
  }

private:
  void write_body() {
    output_ << "{\n";
    const bool straight_line = body_->blocks.size() == 1U &&
                               body_->blocks.front().name == "entry" &&
                               body_->blocks.front().arguments.empty() &&
                               (!body_->blocks.front().terminator ||
                                body_->blocks.front().terminator->kind ==
                                    detail::TermSyntax::Kind::Return);
    if (straight_line) {
      const detail::BlkSyntax& entry = body_->blocks.front();
      for (const auto& statement : entry.statements) {
        write_statement(statement, indent_ + 1U);
      }
      if (entry.terminator) {
        write_terminator(*entry.terminator, indent_ + 1U);
      }
    } else {
      for (std::size_t index = 0; index < body_->blocks.size(); ++index) {
        const detail::BlkSyntax& block = body_->blocks[index];
        output_ << spaces(indent_ + 1U) << block.name << '(';
        for (std::size_t argument = 0; argument < block.arguments.size();
             ++argument) {
          if (argument != 0U) {
            output_ << ", ";
          }
          output_ << block.arguments[argument].name << ": ";
          output_ << detail::format_expression(
              block.arguments[argument].type.value);
        }
        output_ << "):\n";
        for (const auto& statement : block.statements) {
          write_statement(statement, indent_ + 2U);
        }
        if (block.terminator) {
          write_terminator(*block.terminator, indent_ + 2U);
        }
        if (index + 1U != body_->blocks.size()) {
          output_ << '\n';
        }
      }
    }
    output_ << spaces(indent_) << "}\n";
  }

  void write_successor(const detail::SuccessorSyntax& successor) {
    output_ << successor.target << '(';
    for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      output_ << detail::format_expression(successor.arguments[index].value);
    }
    output_ << ')';
  }

  void write_terminator(const detail::TermSyntax& terminator,
                        std::size_t level) {
    output_ << spaces(level);
    switch (terminator.kind) {
    case detail::TermSyntax::Kind::Return:
      output_ << "return";
      for (std::size_t index = 0; index < terminator.values.size(); ++index) {
        output_ << (index == 0U ? " " : ", ")
                << detail::format_expression(terminator.values[index].value);
      }
      break;
    case detail::TermSyntax::Kind::Jump:
      output_ << "jump ";
      if (!terminator.successors.empty()) {
        write_successor(terminator.successors.front());
      }
      break;
    case detail::TermSyntax::Kind::Branch:
      output_ << "branch ";
      if (terminator.condition) {
        output_ << detail::format_expression(terminator.condition->value);
      }
      for (const detail::SuccessorSyntax& successor : terminator.successors) {
        output_ << ", ";
        write_successor(successor);
      }
      break;
    }
    output_ << ";\n";
  }

  static constexpr std::size_t line_limit = 88U;

  static std::string spaces(std::size_t level) {
    return std::string(level * 2U, ' ');
  }

  void write_value(const detail::ValSyntax& value) {
    switch (value.kind) {
    case detail::ValSyntax::Kind::Number:
    case detail::ValSyntax::Kind::Boolean:
      output_ << value.text;
      return;
    case detail::ValSyntax::Kind::String:
      output_ << escape(value.text);
      return;
    case detail::ValSyntax::Kind::List:
      output_ << '[';
      write_values(value.elements);
      output_ << ']';
      return;
    case detail::ValSyntax::Kind::Reference:
      output_ << value.text;
      if (!value.elements.empty()) {
        output_ << '<';
        write_values(value.elements);
        output_ << '>';
      }
      return;
    }
  }

  void write_values(const std::vector<detail::ValSyntax>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      write_value(values[index]);
    }
  }

  void write_arguments(const std::vector<detail::FnArgSyntax>& arguments) {
    output_ << '(';
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      output_ << arguments[index].name << ": ";
      write_value(arguments[index].type);
    }
    output_ << ')';
  }

  void write_type_list(const std::vector<detail::ValSyntax>& types) {
    if (types.size() == 1U) {
      write_value(types.front());
      return;
    }
    output_ << '(';
    write_values(types);
    output_ << ')';
  }

  static std::size_t value_width(const detail::ValSyntax& value) {
    if (value.kind == detail::ValSyntax::Kind::String) {
      return escape(value.text).size();
    }
    if (value.kind == detail::ValSyntax::Kind::List) {
      std::size_t width = 2U;
      for (std::size_t index = 0; index < value.elements.size(); ++index) {
        width += value_width(value.elements[index]) + (index == 0U ? 0U : 2U);
      }
      return width;
    }
    std::size_t width = value.text.size();
    if (!value.elements.empty()) {
      width += 2U;
      for (std::size_t index = 0; index < value.elements.size(); ++index) {
        width += value_width(value.elements[index]) + (index == 0U ? 0U : 2U);
      }
    }
    return width;
  }

  void write_statement(const detail::StatementSyntax& statement,
                       std::size_t level) {
    if (statement.kind == detail::StatementSyntax::Kind::Break ||
        statement.kind == detail::StatementSyntax::Kind::Continue) {
      output_ << spaces(level)
              << (statement.kind == detail::StatementSyntax::Kind::Break
                      ? "break;\n"
                      : "continue;\n");
      return;
    }
    if (statement.kind == detail::StatementSyntax::Kind::Return) {
      output_ << spaces(level) << "return";
      for (std::size_t index = 0; index < statement.values.size(); ++index) {
        output_ << (index == 0U ? " " : ", ")
                << detail::format_expression(statement.values[index].value);
      }
      output_ << ";\n";
      return;
    }
    if (statement.kind == detail::StatementSyntax::Kind::If) {
      output_ << spaces(level) << "if "
              << detail::format_expression(statement.expression.value)
              << " {\n";
      for (const auto& nested : statement.body) {
        write_statement(nested, level + 1U);
      }
      output_ << spaces(level) << '}';
      if (statement.has_else) {
        output_ << " else {\n";
        for (const auto& nested : statement.otherwise) {
          write_statement(nested, level + 1U);
        }
        output_ << spaces(level) << '}';
      }
      output_ << '\n';
      return;
    }
    if (statement.kind == detail::StatementSyntax::Kind::While) {
      output_ << spaces(level) << "while "
              << detail::format_expression(statement.expression.value)
              << " {\n";
      for (const auto& nested : statement.body) {
        write_statement(nested, level + 1U);
      }
      output_ << spaces(level) << "}\n";
      return;
    }
    if (statement.kind == detail::StatementSyntax::Kind::For) {
      output_ << spaces(level) << "for " << statement.iterator->name;
      if (statement.iterator->type) {
        output_ << ": "
                << detail::format_expression(statement.iterator->type->value);
      }
      output_ << " in " << detail::format_expression(statement.expression.value)
              << " {\n";
      for (const auto& nested : statement.body) {
        write_statement(nested, level + 1U);
      }
      output_ << spaces(level) << "}\n";
      return;
    }
    std::size_t prefix_width = 0U;
    output_ << spaces(level);
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
        prefix_width += 2U;
      }
      output_ << statement.bindings[index].name;
      prefix_width += statement.bindings[index].name.size();
      if (statement.bindings[index].type) {
        output_ << ": ";
        const std::string type =
            detail::format_expression(statement.bindings[index].type->value);
        prefix_width += 2U + type.size();
        output_ << type;
      }
    }
    if (!statement.bindings.empty()) {
      output_ << " = ";
      prefix_width += 3U;
    }
    const Mod::Expr& expression = statement.expression.value;
    const std::string flat = detail::format_expression(expression);
    const bool multiline_call =
        expression.kind == Mod::Expr::Kind::Call &&
        expression.arguments.size() > 1U &&
        level * 2U + prefix_width + flat.size() > line_limit;
    if (!multiline_call) {
      output_ << flat;
    } else {
      output_ << expression.text << "(\n";
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        output_ << spaces(level + 1U);
        if (index < expression.labels.size() &&
            !expression.labels[index].empty()) {
          output_ << expression.labels[index] << ": ";
        }
        output_ << detail::format_expression(expression.arguments[index]);
        if (index + 1U != expression.arguments.size()) {
          output_ << ',';
        }
        output_ << '\n';
      }
      output_ << spaces(level) << ')';
    }
    output_ << ";\n";
  }

  const detail::FnSyntax* fn_ = nullptr;
  const detail::FnBody* body_ = nullptr;
  std::size_t indent_ = 0;
  std::ostringstream output_;
};

class RuntimeSyntax {
public:
  RuntimeSyntax(const Fn& fn, std::string_view name, bool qualify_types)
      : fn_(fn), qualify_types_(qualify_types) {
    syntax_.name = std::string(name);
  }

  detail::FnSyntax build() {
    for (const Val& argument : fn_.arguments()) {
      const std::string name = bind(argument, "arg");
      syntax_.arguments.push_back({name, value(argument.type())});
    }
    for (const Type& result : fn_.result_types()) {
      syntax_.result_types.push_back(value(result));
    }
    const auto blocks = fn_.blks();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      block_names_.emplace_back(blocks[index],
                                index == 0U ? "entry"
                                            : "block" + std::to_string(index));
    }
    for (const Blk& block : blocks) {
      for (const Val& argument : block.arguments()) {
        bind(argument, "arg");
      }
    }
    for (const Blk& block : blocks) {
      for (const Op& op : block.ops()) {
        if (!op.callee().referenced_fn()) {
          remember_fn(op.callee());
        }
      }
      const Term terminator = block.terminator();
      if (const auto condition = terminator.condition()) {
        remember_fn(*condition);
      }
      for (const Val& returned : terminator.returned()) {
        remember_fn(returned);
      }
      for (std::size_t index = 0; index < terminator.successor_count();
           ++index) {
        for (const Val& argument : terminator.arguments(index)) {
          remember_fn(argument);
        }
      }
    }
    for (const Blk& block : blocks) {
      detail::BlkSyntax syntax;
      syntax.name = block_name(block);
      for (const Val& argument : block.arguments()) {
        syntax.arguments.push_back(
            {use(argument), type_expression(argument.type()), {}});
      }
      if (block.is_entry()) {
        for (const Val& fn : fns_) {
          syntax.statements.push_back(fn_binding(fn));
        }
      }
      for (const Op& op : block.ops()) {
        syntax.statements.push_back(convert(op));
      }
      syntax.terminator = convert(block.terminator());
      syntax_.body.blocks.push_back(std::move(syntax));
    }
    return std::move(syntax_);
  }

private:
  static bool source_order(const Op& op, const Mod::FnDecl& declaration,
                           Mod::Expr& expression) {
    std::vector<std::pair<std::size_t, std::size_t>> order;
    const auto arguments = op.arguments();
    order.reserve(arguments.size() + op.callee().bindings().size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      order.emplace_back(detail::FnAccess::argument_parameter(op, index),
                         index);
    }
    const auto parameters = declaration.inputs();
    std::size_t expression_index = arguments.size();
    for (const auto& [name, value] : op.callee().bindings()) {
      (void)value;
      const auto found = std::find_if(
          parameters.begin(), parameters.end(),
          [&](const auto& parameter) { return parameter.name == name; });
      if (found == parameters.end()) {
        return false;
      }
      order.emplace_back(
          static_cast<std::size_t>(std::distance(parameters.begin(), found)),
          expression_index++);
    }
    if (order.size() != expression.arguments.size()) {
      return false;
    }
    std::stable_sort(
        order.begin(), order.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    std::size_t expected = 0;
    for (const auto& [parameter, original] : order) {
      (void)original;
      if (parameter >= parameters.size()) {
        return false;
      }
      if (parameter == expected) {
        ++expected;
        continue;
      }
      if (parameter + 1U != expected || !parameters[parameter].variadic) {
        return false;
      }
    }
    std::vector<Mod::Expr> reordered;
    reordered.reserve(expression.arguments.size());
    for (const auto& [parameter, original] : order) {
      (void)parameter;
      reordered.push_back(std::move(expression.arguments[original]));
    }
    expression.arguments = std::move(reordered);
    expression.labels.assign(expression.arguments.size(), {});
    return true;
  }

  static std::size_t
  visible_parameters(std::span<const ParamVal> parameters,
                     std::span<const Mod::ParamDecl> schema) {
    std::size_t count = parameters.size();
    while (count != 0U && count <= schema.size() &&
           schema[count - 1U].default_value) {
      const auto value = detail::parameter_default(schema[count - 1U]);
      if (!value || parameters[count - 1U] != *value) {
        break;
      }
      --count;
    }
    return count;
  }

  detail::ValSyntax value(const Type& type) const {
    detail::ValSyntax result;
    result.kind = detail::ValSyntax::Kind::Reference;
    result.text = qualify_types_ ? type.schema().symbol().qualified_name()
                                 : detail::display_type_name(
                                       type.schema().symbol().qualified_name());
    const auto parameters = detail::TypeAccess::parameters(type);
    const std::size_t count =
        visible_parameters(parameters, type.schema().parameters());
    for (std::size_t index = 0; index < count; ++index) {
      result.elements.push_back(value(parameters[index]));
    }
    return result;
  }

  detail::ValSyntax value(const ParamVal& parameter) const {
    detail::ValSyntax result;
    switch (parameter.kind()) {
    case ParamVal::Kind::I64:
      result.kind = detail::ValSyntax::Kind::Number;
      result.text = std::to_string(*parameter.as_i64());
      break;
    case ParamVal::Kind::F64: {
      result.kind = detail::ValSyntax::Kind::Number;
      const auto text = detail::canonical_real(*parameter.as_f64());
      if (!text) {
        throw std::invalid_argument(
            "the Fn contains an unformattable real parameter");
      }
      result.text = *text;
      break;
    }
    case ParamVal::Kind::Boolean:
      result.kind = detail::ValSyntax::Kind::Boolean;
      result.text = *parameter.as_bool() ? "true" : "false";
      break;
    case ParamVal::Kind::String:
      result.kind = detail::ValSyntax::Kind::String;
      result.text = *parameter.as_string();
      break;
    case ParamVal::Kind::Type:
      return value(*parameter.as_type());
    case ParamVal::Kind::List:
      result.kind = detail::ValSyntax::Kind::List;
      for (const ParamVal& element : parameter.elements()) {
        result.elements.push_back(value(element));
      }
      break;
    }
    return result;
  }

  std::string bind(const Val& value, std::string_view prefix) {
    const std::string name =
        std::string(prefix) + std::to_string(next_value_++);
    names_.emplace_back(value, name);
    return name;
  }

  void remember_fn(const Val& value) {
    if ((!value.referenced_fn() && !value.inline_fn()) ||
        std::find(fns_.begin(), fns_.end(), value) != fns_.end()) {
      return;
    }
    bind(value, "fn");
    fns_.push_back(value);
  }

  detail::StatementSyntax fn_binding(const Val& value) const {
    const auto fn = value.referenced_fn();
    detail::StatementSyntax statement;
    statement.kind = detail::StatementSyntax::Kind::Expr;
    statement.bindings.push_back(
        {use(value), type_expression(value.type()), {}});
    if (fn) {
      statement.expression.value.kind = Mod::Expr::Kind::Reference;
      statement.expression.value.text = fn->symbol().qualified_name();
      return statement;
    }
    const auto body = value.inline_fn();
    std::vector<Mod::Expr> captures;
    for (const Val& capture : value.captures()) {
      auto expression = value_expression(capture);
      if (!expression) {
        throw std::logic_error("inline fn capture cannot be formatted");
      }
      captures.push_back(std::move(*expression));
    }
    const auto lambda = body ? inline_expression(*body, std::move(captures))
                             : std::optional<Mod::Expr>{};
    if (!lambda) {
      throw std::logic_error(
          "inline fn cannot be represented as one expression");
    }
    statement.expression.value = *lambda;
    return statement;
  }

  std::optional<Mod::Expr>
  inline_expression(const Fn& fn, std::vector<Mod::Expr> captures = {}) const {
    using Kind = Mod::Expr::Kind;
    const auto blocks = fn.blks();
    const auto returned = blocks.size() == 1U
                              ? blocks.front().terminator().returned()
                              : std::vector<Val>{};
    if (returned.size() != 1U || !blocks.front().arguments().empty()) {
      return std::nullopt;
    }
    const auto arguments = fn.arguments();
    if (captures.size() > arguments.size()) {
      return std::nullopt;
    }
    const std::size_t visible_arguments = arguments.size() - captures.size();
    std::unordered_set<std::string> reserved;
    const auto collect_names = [&](const auto& self,
                                   const Mod::Expr& expression) -> void {
      if (!expression.text.empty()) {
        reserved.insert(expression.text);
      }
      for (const Mod::Expr& argument : expression.arguments) {
        self(self, argument);
      }
    };
    for (const Mod::Expr& capture : captures) {
      collect_names(collect_names, capture);
    }
    std::vector<std::string> names;
    names.reserve(visible_arguments);
    for (std::size_t index = 0; index < visible_arguments; ++index) {
      std::string name = "arg" + std::to_string(index);
      while (reserved.contains(name)) {
        name += '_';
      }
      reserved.insert(name);
      names.push_back(std::move(name));
    }
    std::function<std::optional<Mod::Expr>(const Val&)> build =
        [&](const Val& current) -> std::optional<Mod::Expr> {
      const auto argument =
          std::find(arguments.begin(), arguments.end(), current);
      if (argument != arguments.end()) {
        const std::size_t index = static_cast<std::size_t>(
            std::distance(arguments.begin(), argument));
        if (index < visible_arguments) {
          return Mod::Expr{Kind::Variable, names[index], {}};
        }
        return captures[index - visible_arguments];
      }
      if (const auto reference = current.referenced_fn()) {
        return Mod::Expr::reference(
            std::string(reference->symbol().qualified_name()));
      }
      if (const auto nested = current.inline_fn()) {
        std::vector<Mod::Expr> nested_captures;
        for (const Val& capture : current.captures()) {
          auto expression = build(capture);
          if (!expression) {
            return std::nullopt;
          }
          nested_captures.push_back(std::move(*expression));
        }
        return inline_expression(*nested, std::move(nested_captures));
      }
      if (current.known()) {
        const auto payload = detail::FnAccess::known_value(current);
        return payload ? std::optional<Mod::Expr>{expression(value(*payload))}
                       : std::nullopt;
      }
      const auto producer = current.defining_op();
      if (!producer || producer->results().size() != 1U ||
          producer->result(0) != current || current.users().size() > 1U) {
        return std::nullopt;
      }
      Mod::Expr result;
      result.kind = Kind::Call;
      const Val callee_value = producer->callee();
      const auto callee = callee_value.referenced_fn();
      if (!callee) {
        return std::nullopt;
      }
      result.text = callee->symbol().qualified_name();
      for (const Val& operand : producer->arguments()) {
        auto built = build(operand);
        if (!built) {
          return std::nullopt;
        }
        result.arguments.push_back(std::move(*built));
        result.labels.emplace_back();
      }
      for (const auto& [name, binding] : callee_value.bindings()) {
        const auto payload = detail::FnAccess::known_value(binding);
        if (!payload) {
          return std::nullopt;
        }
        result.arguments.push_back(expression(value(*payload)));
        result.labels.push_back(name);
      }
      const auto fixity = callee->operator_fixity();
      const bool ordered = source_order(*producer, *callee, result);
      if (fixity && ordered &&
          ((fixity == Mod::FnDecl::Fixity::Infix &&
            result.arguments.size() == 2U) ||
           (fixity != Mod::FnDecl::Fixity::Infix &&
            result.arguments.size() == 1U))) {
        result.text = std::string(callee->name());
        result.labels.clear();
        result.kind = fixity == Mod::FnDecl::Fixity::Prefix  ? Kind::Prefix
                      : fixity == Mod::FnDecl::Fixity::Infix ? Kind::Infix
                                                             : Kind::Postfix;
      }
      return result;
    };
    auto body = build(returned.front());
    if (!body) {
      return std::nullopt;
    }
    Mod::Expr lambda;
    lambda.kind = Kind::Lambda;
    lambda.labels = std::move(names);
    for (std::size_t index = 0; index < visible_arguments; ++index) {
      lambda.arguments.push_back(expression(value(arguments[index].type())));
    }
    lambda.arguments.push_back(std::move(*body));
    return lambda;
  }

  std::optional<Mod::Expr> value_expression(const Val& current) const {
    using Kind = Mod::Expr::Kind;
    if (const auto reference = current.referenced_fn()) {
      return Mod::Expr::reference(
          std::string(reference->symbol().qualified_name()));
    }
    if (const auto nested = current.inline_fn()) {
      std::vector<Mod::Expr> captures;
      for (const Val& capture : current.captures()) {
        auto expression = value_expression(capture);
        if (!expression) {
          return std::nullopt;
        }
        captures.push_back(std::move(*expression));
      }
      return inline_expression(*nested, std::move(captures));
    }
    if (current.known()) {
      const auto payload = detail::FnAccess::known_value(current);
      return payload ? std::optional<Mod::Expr>{expression(value(*payload))}
                     : std::nullopt;
    }
    return Mod::Expr{Kind::Variable, use(current), {}};
  }

  std::string use(const Val& value) const {
    const auto found =
        std::find_if(names_.begin(), names_.end(),
                     [&](const auto& entry) { return entry.first == value; });
    if (found == names_.end()) {
      if (const auto fn = value.referenced_fn()) {
        return fn->symbol().qualified_name();
      }
    }
    if (found == names_.end()) {
      throw std::invalid_argument(
          "the fn contains a value before its definition");
    }
    return found->second;
  }

  std::string block_name(const Blk& block) const {
    const auto found =
        std::find_if(block_names_.begin(), block_names_.end(),
                     [&](const auto& entry) { return entry.first == block; });
    if (found == block_names_.end()) {
      throw std::invalid_argument("the fn has an unknown successor");
    }
    return found->second;
  }

  static Mod::Expr expression(const detail::ValSyntax& value) {
    using Kind = Mod::Expr::Kind;
    Mod::Expr result;
    switch (value.kind) {
    case detail::ValSyntax::Kind::Number:
      result.kind = Kind::Number;
      break;
    case detail::ValSyntax::Kind::Boolean:
      result.kind = Kind::Boolean;
      break;
    case detail::ValSyntax::Kind::String:
      result.kind = Kind::String;
      break;
    case detail::ValSyntax::Kind::List:
      result.kind = Kind::List;
      break;
    case detail::ValSyntax::Kind::Reference:
      result.kind = Kind::Reference;
      break;
    }
    result.text = value.text;
    for (const auto& element : value.elements) {
      result.arguments.push_back(expression(element));
    }
    return result;
  }

  detail::ExprSyntax type_expression(const Type& type) const {
    return {expression(value(type)), {}};
  }

  detail::StatementSyntax convert(const Op& op) {
    detail::StatementSyntax result;
    result.expression.value.kind = Mod::Expr::Kind::Call;
    const Val callee_value = op.callee();
    const auto declaration = callee_value.referenced_fn();
    result.expression.value.text = declaration
                                       ? declaration->symbol().qualified_name()
                                       : use(callee_value);
    for (const Val& output : op.results()) {
      result.bindings.push_back(
          {bind(output, "v"), type_expression(output.type()), {}});
    }
    const auto arguments = op.arguments();
    const auto parameters =
        declaration ? declaration->inputs() : std::span<const Mod::ParamDecl>{};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const Val& argument = arguments[index];
      const std::size_t parameter_index =
          detail::FnAccess::argument_parameter(op, index);
      if (argument.known()) {
        const auto payload = detail::FnAccess::known_value(argument);
        if (!payload || (declaration && parameter_index >= parameters.size())) {
          throw std::logic_error("op has an invalid Known argument");
        }
        result.expression.value.arguments.push_back(
            expression(value(*payload)));
        result.expression.value.labels.emplace_back();
      } else {
        auto expression = value_expression(argument);
        if (!expression) {
          throw std::logic_error("call argument cannot be formatted");
        }
        result.expression.value.arguments.push_back(std::move(*expression));
        result.expression.value.labels.emplace_back();
      }
    }
    for (const auto& [name, binding] : callee_value.bindings()) {
      const auto payload = detail::FnAccess::known_value(binding);
      if (!payload) {
        throw std::logic_error("callee has an invalid compile-time binding");
      }
      result.expression.value.arguments.push_back(expression(value(*payload)));
      result.expression.value.labels.push_back(name);
    }
    const bool ordered =
        declaration && source_order(op, *declaration, result.expression.value);
    const auto fixity = declaration ? declaration->operator_fixity()
                                    : std::optional<Mod::FnDecl::Fixity>{};
    const bool valid_arity =
        fixity && ((*fixity == Mod::FnDecl::Fixity::Infix &&
                    result.expression.value.arguments.size() == 2U) ||
                   (*fixity != Mod::FnDecl::Fixity::Infix &&
                    result.expression.value.arguments.size() == 1U));
    if (fixity && valid_arity && result.bindings.size() == 1U && ordered) {
      result.expression.value.text = std::string(declaration->name());
      if (*fixity == Mod::FnDecl::Fixity::Prefix) {
        result.expression.value.kind = Mod::Expr::Kind::Prefix;
      } else if (*fixity == Mod::FnDecl::Fixity::Infix) {
        result.expression.value.kind = Mod::Expr::Kind::Infix;
      } else {
        result.expression.value.kind = Mod::Expr::Kind::Postfix;
      }
      result.expression.value.labels.clear();
    }
    return result;
  }

  detail::TermSyntax convert(const Term& terminator) const {
    detail::TermSyntax result;
    if (terminator.kind() == Term::Kind::Return) {
      result.kind = detail::TermSyntax::Kind::Return;
      for (const Val& value : terminator.returned()) {
        result.values.push_back({Mod::Expr::reference(use(value)), {}});
      }
      return result;
    }
    result.kind = terminator.kind() == Term::Kind::Jump
                      ? detail::TermSyntax::Kind::Jump
                      : detail::TermSyntax::Kind::Branch;
    if (const auto condition = terminator.condition()) {
      result.condition =
          detail::ExprSyntax{Mod::Expr::reference(use(*condition)), {}};
    }
    for (std::size_t index = 0; index < terminator.successor_count(); ++index) {
      detail::SuccessorSyntax successor;
      successor.target = block_name(terminator.successor(index));
      for (const Val& argument : terminator.arguments(index)) {
        successor.arguments.push_back(
            {Mod::Expr::reference(use(argument)), {}});
      }
      result.successors.push_back(std::move(successor));
    }
    return result;
  }

  const Fn& fn_;
  detail::FnSyntax syntax_;
  std::vector<std::pair<Val, std::string>> names_;
  std::vector<Val> fns_;
  std::vector<std::pair<Blk, std::string>> block_names_;
  std::size_t next_value_ = 0;
  bool qualify_types_ = false;
};

}  // namespace

namespace detail {

std::optional<FnBody>
parse_fn_body(Lexer& lexer, Token& current, Diag& diagnostics,
              std::string source,
              std::span<const Mod::FnDecl::GenericDecl> variables) {
  return SyntaxParser(lexer, current, diagnostics, std::move(source), variables)
      .parse();
}

std::string format_fn_body(const FnBody& body, std::size_t indent) {
  return SyntaxWriter(body, indent).write();
}

std::string format_fn_syntax(const FnSyntax& fn, std::size_t indent) {
  return SyntaxWriter(fn, indent).write();
}

FnSyntax materialized_fn_syntax(const Fn& fn, std::string_view name,
                                bool qualify_types) {
  return RuntimeSyntax(fn, name, qualify_types).build();
}

}  // namespace detail

std::string format(const Fn& fn, std::string_view name) {
  const auto identifier_character = [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  };
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_') ||
      !std::all_of(name.begin() + 1, name.end(), identifier_character)) {
    throw std::invalid_argument("a formatted Fn needs a valid name");
  }
  return detail::format_fn_syntax(detail::materialized_fn_syntax(fn, name), 0U);
}

}  // namespace joggle
