#include "function_body.h"

#include "call_resolution.h"
#include "expression_syntax.h"
#include "prelude.h"
#include "prelude_runtime.h"
#include "compiler_internal.h"

#include "diagnostic_internal.h"
#include "domain.h"
#include "execution.h"
#include "ir_internal.h"
#include "joggle/compiler.h"
#include "module_internal.h"
#include "syntax_lexer.h"
#include "type_contract.h"
#include "type_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <locale>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace joggle {
namespace {

using detail::ParameterValue;
using TokenKind = detail::TokenKind;
using Token = detail::Token;
using Lexer = detail::Lexer;

class SyntaxParser {
public:
  SyntaxParser(Lexer& lexer, Token& current, Diagnostics& diagnostics,
               std::string source,
               std::span<const Module::FunctionDecl::GenericDecl> variables)
      : lexer_(lexer), diagnostics_(diagnostics), source_(std::move(source)),
        initial_diagnostics_(diagnostics.size()), current_(current),
        variables_(variables.begin(), variables.end()) {
    for (const auto& variable : variables_) {
      locals_.insert(variable.name);
    }
  }

  std::optional<detail::FunctionBody> parse() {
    const SourcePosition begin = current_.begin;
    detail::FunctionBody body;
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
      detail::BlockSyntax entry;
      entry.name = "entry";
      entry.range.begin = current_.begin;
      while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
        if (is_name("jump") || is_name("branch")) {
          error("jump and branch require explicit Blocks");
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
    if (!detail::verify_function_body(body, diagnostics_)) {
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

  detail::ExpressionSyntax expression() {
    const SourcePosition begin = current_.begin;
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
    const SourcePosition begin = current_.begin;
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

  detail::TerminatorSyntax parse_terminator() {
    detail::TerminatorSyntax terminator;
    const SourcePosition begin = current_.begin;
    if (match_name("return")) {
      terminator.kind = detail::TerminatorSyntax::Kind::Return;
      if (!is(TokenKind::Semicolon)) {
        do {
          terminator.values.push_back(expression());
        } while (match(TokenKind::Comma));
      }
    } else if (match_name("jump")) {
      terminator.kind = detail::TerminatorSyntax::Kind::Jump;
      terminator.successors.push_back(parse_successor());
    } else if (match_name("branch")) {
      terminator.kind = detail::TerminatorSyntax::Kind::Branch;
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

  detail::BlockSyntax parse_block() {
    detail::BlockSyntax block;
    const SourcePosition begin = current_.begin;
    if (auto block_name = name("a block name")) {
      block.name = std::move(*block_name);
    }
    expect(TokenKind::LeftParen, "'('");
    if (!match(TokenKind::RightParen)) {
      do {
        detail::BlockArgumentSyntax argument;
        const SourcePosition argument_begin = current_.begin;
        if (auto argument_name = local_name()) {
          argument.name = std::move(*argument_name);
        }
        expect(TokenKind::Colon, "':'");
        argument.type = expression();
        argument.range = {argument_begin, previous_end_};
        variables_.push_back({argument.name,
                              Module::Expression::reference("type")});
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
    const SourcePosition begin = current_.begin;
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
            error("jump and branch are only available in explicit Blocks");
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
          error("jump and branch are only available in explicit Blocks");
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
      const SourcePosition iterator_begin = current_.begin;
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
            {statement.iterator->name,
             statement.iterator->type
                 ? statement.iterator->type->value
                 : Module::Expression::reference("type")});
      }
      ++loop_depth_;
      while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
        if (is_name("jump") || is_name("branch")) {
          error("jump and branch are only available in explicit Blocks");
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
      auto add_binding = [&](std::string name, SourcePosition binding_begin) {
        detail::BindingSyntax binding;
        binding.name = std::move(name);
        binding.rebind = locals_.contains(binding.name);
        if (match(TokenKind::Colon)) {
          binding.type = expression();
        }
        binding.range = {binding_begin, previous_end_};
        if (!binding.rebind) {
          locals_.insert(binding.name);
          variables_.push_back({binding.name,
                                Module::Expression::reference("type")});
        }
        statement.bindings.push_back(std::move(binding));
      };
      const SourcePosition first_begin = current_.begin;
      if (auto first = local_name()) {
        add_binding(std::move(*first), first_begin);
      }
      while (match(TokenKind::Comma)) {
        const SourcePosition binding_begin = current_.begin;
        if (auto binding = local_name()) {
          add_binding(std::move(*binding), binding_begin);
        }
      }
      expect(TokenKind::Equal, "'='");
    }
    statement.expression = expression();
    if (is(TokenKind::LeftBrace)) {
      error("a call cannot own a nested body; pass a named function as an "
            "ordinary argument");
    }
    expect(TokenKind::Semicolon, "';'");
    statement.range = {begin, previous_end_};
    return statement;
  }

  void error(std::string message) {
    diagnostics_.report(std::move(message),
                        SourceRange{source_, current_.begin, current_.end});
  }

  Lexer& lexer_;
  Diagnostics& diagnostics_;
  std::string source_;
  std::size_t initial_diagnostics_ = 0;
  Token& current_;
  SourcePosition previous_end_;
  std::vector<Module::FunctionDecl::GenericDecl> variables_;
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
  SyntaxWriter(const detail::FunctionSyntax& function, std::size_t indent)
      : function_(&function), body_(&function.body), indent_(indent) {}

  SyntaxWriter(const detail::FunctionBody& body, std::size_t indent)
      : body_(&body), indent_(indent) {}

  std::string write() {
    if (function_ == nullptr) {
      write_body();
      return output_.str();
    }
    output_ << spaces(indent_) << "fn " << function_->name;
    std::size_t signature_width =
        indent_ * 2U + 3U + function_->name.size() + 2U;
    for (std::size_t index = 0; index < function_->arguments.size(); ++index) {
      signature_width += function_->arguments[index].name.size() + 3U +
                         value_width(function_->arguments[index].type) +
                         (index == 0U ? 0U : 2U);
    }
    if (!function_->result_types.empty()) {
      signature_width += 4U;
      if (function_->result_types.size() > 1U) {
        signature_width += 2U;
      }
      for (std::size_t index = 0; index < function_->result_types.size();
           ++index) {
        signature_width += value_width(function_->result_types[index]) +
                           (index == 0U ? 0U : 2U);
      }
    }
    const bool multiline_arguments =
        !function_->arguments.empty() && signature_width > line_limit;
    if (multiline_arguments) {
      output_ << "(\n";
      for (std::size_t index = 0; index < function_->arguments.size();
           ++index) {
        output_ << spaces(indent_ + 1U) << function_->arguments[index].name
                << ": ";
        write_value(function_->arguments[index].type);
        if (index + 1U != function_->arguments.size()) {
          output_ << ',';
        }
        output_ << '\n';
      }
      output_ << spaces(indent_) << ')';
    } else {
      write_arguments(function_->arguments);
    }
    if (!function_->result_types.empty()) {
      output_ << " -> ";
      write_type_list(function_->result_types);
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
                                    detail::TerminatorSyntax::Kind::Return);
    if (straight_line) {
      const detail::BlockSyntax& entry = body_->blocks.front();
      for (const auto& statement : entry.statements) {
        write_statement(statement, indent_ + 1U);
      }
      if (entry.terminator) {
        write_terminator(*entry.terminator, indent_ + 1U);
      }
    } else {
      for (std::size_t index = 0; index < body_->blocks.size(); ++index) {
        const detail::BlockSyntax& block = body_->blocks[index];
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

  void write_terminator(const detail::TerminatorSyntax& terminator,
                        std::size_t level) {
    output_ << spaces(level);
    switch (terminator.kind) {
    case detail::TerminatorSyntax::Kind::Return:
      output_ << "return";
      for (std::size_t index = 0; index < terminator.values.size(); ++index) {
        output_ << (index == 0U ? " " : ", ")
                << detail::format_expression(terminator.values[index].value);
      }
      break;
    case detail::TerminatorSyntax::Kind::Jump:
      output_ << "jump ";
      if (!terminator.successors.empty()) {
        write_successor(terminator.successors.front());
      }
      break;
    case detail::TerminatorSyntax::Kind::Branch:
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

  void write_value(const detail::ValueSyntax& value) {
    switch (value.kind) {
    case detail::ValueSyntax::Kind::Number:
    case detail::ValueSyntax::Kind::Boolean:
      output_ << value.text;
      return;
    case detail::ValueSyntax::Kind::String:
      output_ << escape(value.text);
      return;
    case detail::ValueSyntax::Kind::List:
      output_ << '[';
      write_values(value.elements);
      output_ << ']';
      return;
    case detail::ValueSyntax::Kind::Reference:
      output_ << value.text;
      if (!value.elements.empty()) {
        output_ << '<';
        write_values(value.elements);
        output_ << '>';
      }
      return;
    }
  }

  void write_values(const std::vector<detail::ValueSyntax>& values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      write_value(values[index]);
    }
  }

  void write_arguments(
      const std::vector<detail::FunctionArgumentSyntax>& arguments) {
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

  void write_type_list(const std::vector<detail::ValueSyntax>& types) {
    if (types.size() == 1U) {
      write_value(types.front());
      return;
    }
    output_ << '(';
    write_values(types);
    output_ << ')';
  }

  static std::size_t value_width(const detail::ValueSyntax& value) {
    if (value.kind == detail::ValueSyntax::Kind::String) {
      return escape(value.text).size();
    }
    if (value.kind == detail::ValueSyntax::Kind::List) {
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
                << detail::format_expression(
                       statement.iterator->type->value);
      }
      output_ << " in "
              << detail::format_expression(statement.expression.value)
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
    const Module::Expression& expression = statement.expression.value;
    const std::string flat = detail::format_expression(expression);
    const bool multiline_call =
        expression.kind == Module::Expression::Kind::Call &&
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

  const detail::FunctionSyntax* function_ = nullptr;
  const detail::FunctionBody* body_ = nullptr;
  std::size_t indent_ = 0;
  std::ostringstream output_;
};

std::pair<std::string_view, std::string_view>
split_reference(std::string_view owner, std::string_view reference) {
  const std::size_t dot = reference.find('.');
  return dot == std::string_view::npos
             ? std::pair<std::string_view, std::string_view>{owner, reference}
             : std::pair<std::string_view, std::string_view>{
                   reference.substr(0, dot), reference.substr(dot + 1U)};
}

class Instantiator {
  struct Path {
    Block block;
    detail::Locals locals;
    std::size_t residual_depth = 0;
  };

  struct Flow {
    std::vector<Path> next;
    std::vector<Path> breaks;
    std::vector<Path> continues;

    void append(Flow&& flow) {
      next.insert(next.end(), std::make_move_iterator(flow.next.begin()),
                  std::make_move_iterator(flow.next.end()));
      breaks.insert(breaks.end(), std::make_move_iterator(flow.breaks.begin()),
                    std::make_move_iterator(flow.breaks.end()));
      continues.insert(continues.end(),
                       std::make_move_iterator(flow.continues.begin()),
                       std::make_move_iterator(flow.continues.end()));
    }
  };

  struct LoopContext {
    std::optional<Block> continue_target;
    std::optional<Block> break_target;
    std::vector<std::string> carried_names;
    std::vector<Type> carried_types;
  };

  struct PendingArgument {
    std::optional<detail::StagedValue> value;
    std::string function;
    std::optional<Module::Expression> inline_function;
    std::optional<std::vector<Type>> inline_inputs;

    bool is_function() const { return !function.empty(); }
    bool is_inline_function() const { return inline_function.has_value(); }
    bool valid() const {
      return value.has_value() || is_function() ||
             (is_inline_function() && inline_inputs.has_value());
    }
  };

  struct PendingCall {
    Module::FunctionDecl function;
    std::vector<std::vector<PendingArgument>> arguments;
    detail::CallTypes partial_types;
    std::vector<std::optional<ParameterValue>> known_arguments;
  };

public:
  Instantiator(Compiler& compiler, Module::FunctionDecl function,
               const detail::FunctionBody& body, Diagnostics& diagnostics,
               std::vector<Value> known_arguments,
               detail::KnownBindings bindings)
      : compiler_(compiler), declaration_(std::move(function)), body_(body),
        owner_(declaration_->symbol().module_name()), diagnostics_(diagnostics),
        initial_diagnostics_(diagnostics.size()),
        supplied_known_(std::move(known_arguments)),
        supplied_bindings_(std::move(bindings)) {}

  Instantiator(Compiler& compiler, std::string owner,
               const detail::FunctionBody& body, Diagnostics& diagnostics,
               std::vector<std::pair<std::string, Type>> arguments,
               std::optional<std::vector<Type>> results)
      : compiler_(compiler), body_(body), owner_(std::move(owner)),
        diagnostics_(diagnostics), initial_diagnostics_(diagnostics.size()),
        inline_arguments_(std::move(arguments)),
        inline_results_(std::move(results)) {}

  std::optional<Function> instantiate() {
    if (!declaration_) {
      return instantiate_inline();
    }
    function_ = compiler_.create_function();
    if (!function_) {
      return std::nullopt;
    }
    const auto& contract = detail::FunctionTypeAccess::get(*declaration_);
    const auto parameters = declaration_->inputs();
    std::vector<std::optional<detail::StagedValue>> known_parameters(
        parameters.size());
    detail::KnownBindings bindings = supplied_bindings_;
    std::size_t supplied = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        continue;
      }
      const Module::ParameterDecl& parameter = parameters[index];
      std::size_t required_after = 0;
      for (std::size_t next = index + 1U; next < parameters.size(); ++next) {
        if (!detail::is_value_port(parameters[next]) &&
            !parameters[next].default_value) {
          ++required_after;
        }
      }
      const bool use_default =
          parameter.default_value &&
          supplied_known_.size() - supplied == required_after;
      std::optional<detail::StagedValue> value;
      if (!use_default && supplied < supplied_known_.size()) {
        value = detail::stage(supplied_known_[supplied++]);
      } else if (parameter.default_value) {
        auto payload = detail::parameter_default(parameter);
        auto execution = payload ? detail::execution_value(*payload, parameter)
                                 : std::optional<detail::ExecutionValue>{};
        value = execution ? detail::stage(compiler_, std::move(*execution))
                          : std::optional<detail::StagedValue>{};
      }
      const detail::ExecutionValue* known =
          value && value->known() ? value->known_value() : nullptr;
      const auto payload = known ? detail::parameter_value(*known)
                                 : std::optional<ParameterValue>{};
      if (!value || !payload ||
          !detail::matches_parameter(parameter, *payload)) {
        report("function specialization needs a compatible Known argument '" +
                   parameter.name + "'",
               body_.range);
        continue;
      }
      known_parameters[index] = *value;
      bindings.insert_or_assign(
          parameter.name, detail::KnownBinding{*payload, parameter.domain});
      if (index < contract.bindings.size() && contract.bindings[index] &&
          contract.bindings[index]->kind ==
              Module::Expression::Kind::Variable) {
        bindings.insert_or_assign(
            contract.bindings[index]->text,
            detail::KnownBinding{*payload, parameter.domain});
      }
    }
    if (supplied != supplied_known_.size()) {
      report("function specialization has too many Known arguments",
             body_.range);
    }
    if (!ok()) {
      return std::nullopt;
    }

    const auto resolve_type =
        [&](const Module::Expression& expression,
            std::string_view role) -> std::optional<Type> {
      const Module::ParameterDecl expected{
          std::string(role), detail::domain_expression(detail::ValueKind::Type),
          false, std::nullopt};
      auto value = detail::evaluate_known_expression(
          compiler_, owner_, expression, expected, bindings, diagnostics_,
          source(body_.range));
      const Type* type = value ? value->as_type() : nullptr;
      if (type == nullptr) {
        report("cannot resolve " + std::string(role) +
                   " type during "
                   "function specialization",
               body_.range);
        return std::nullopt;
      }
      return *type;
    };

    result_types_.clear();
    const auto results = detail::value_results(*declaration_);
    for (const auto& result : results) {
      if (auto result_type = resolve_type(result.domain, "result")) {
        result_types_.push_back(*result_type);
      }
    }
    const auto constrain_returns = [&](const auto& self,
                                       const auto& statements) -> void {
      for (const detail::StatementSyntax& statement : statements) {
        if (statement.kind == detail::StatementSyntax::Kind::Return &&
            statement.values.size() == result_types_.size()) {
          for (std::size_t index = 0; index < statement.values.size();
               ++index) {
            const auto& expression = statement.values[index];
            if ((expression.value.kind != Module::Expression::Kind::Reference &&
                 expression.value.kind != Module::Expression::Kind::Variable) ||
                !expression.value.arguments.empty()) {
              continue;
            }
            const auto [found, inserted] = expected_values_.emplace(
                expression.value.text, result_types_[index]);
            if (!inserted && found->second != result_types_[index]) {
              report("one returned value is constrained to different types",
                     expression.range);
            }
          }
        }
        self(self, statement.body);
        self(self, statement.otherwise);
      }
    };
    for (const detail::BlockSyntax& block : body_.blocks) {
      if (block.terminator &&
          block.terminator->kind == detail::TerminatorSyntax::Kind::Return &&
          block.terminator->values.size() == result_types_.size()) {
        for (std::size_t index = 0; index < block.terminator->values.size();
             ++index) {
          const auto& expression = block.terminator->values[index];
          if ((expression.value.kind != Module::Expression::Kind::Reference &&
               expression.value.kind != Module::Expression::Kind::Variable) ||
              !expression.value.arguments.empty()) {
            continue;
          }
          const auto [found, inserted] = expected_values_.emplace(
              expression.value.text, result_types_[index]);
          if (!inserted && found->second != result_types_[index]) {
            report("one returned value is constrained to different types",
                   expression.range);
          }
        }
      }
      constrain_returns(constrain_returns, block.statements);
    }
    std::vector<Type> argument_types;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        if (auto argument_type =
                resolve_type(parameters[index].domain, "input")) {
          argument_types.push_back(*argument_type);
        }
      }
    }
    if (argument_types.size() != detail::value_inputs(*declaration_).size() ||
        result_types_.size() != results.size()) {
      return std::nullopt;
    }

    detail::FunctionAccess::declare(*function_, *declaration_, argument_types,
                                    result_types_);
    edit_.emplace(function_->edit());
    locals_.push();
    std::size_t residual = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        define(parameters[index].name,
               edit_->argument(argument_types[residual++]), body_.range);
      } else if (known_parameters[index]) {
        define_staged(parameters[index].name, *known_parameters[index],
                      body_.range);
      }
    }
    for (const auto& generic : declaration_->generics()) {
      const auto found = bindings.find(generic.name);
      if (found == bindings.end() || locals_.contains(generic.name)) {
        continue;
      }
      const Module::ParameterDecl parameter{generic.name, generic.domain, false,
                                            std::nullopt};
      auto value = detail::execution_value(found->second.value, parameter);
      auto staged = value ? detail::stage(compiler_, std::move(*value))
                          : std::optional<detail::StagedValue>{};
      if (!staged) {
        report("generic '" + generic.name + "' cannot enter staged evaluation",
               body_.range);
        continue;
      }
      define_staged(generic.name, std::move(*staged), body_.range);
    }

    blocks_.emplace("entry", function_->entry());
    for (std::size_t index = 1; index < body_.blocks.size(); ++index) {
      const detail::BlockSyntax& block = body_.blocks[index];
      std::vector<Type> block_argument_types;
      for (const detail::BlockArgumentSyntax& argument : block.arguments) {
        if (auto argument_type = type(argument.type)) {
          block_argument_types.push_back(*argument_type);
        }
      }
      if (block_argument_types.size() == block.arguments.size()) {
        blocks_.emplace(block.name,
                        edit_->block(std::move(block_argument_types)));
      }
    }

    for (const detail::BlockSyntax& block : body_.blocks) {
      const auto ir = blocks_.find(block.name);
      if (ir == blocks_.end()) {
        continue;
      }
      const auto arguments = ir->second.arguments();
      for (std::size_t index = 0;
           index < block.arguments.size() && index < arguments.size();
           ++index) {
        define(block.arguments[index].name, arguments[index],
               block.arguments[index].range);
      }
    }

    for (const detail::BlockSyntax& block : body_.blocks) {
      const auto ir = blocks_.find(block.name);
      if (ir == blocks_.end()) {
        continue;
      }
      Flow current = instantiate_sequence(block.statements, ir->second);
      if (!current.breaks.empty() || !current.continues.empty()) {
        report("loop control escaped its structured loop", block.range);
        continue;
      }
      if (current.next.empty()) {
        continue;
      }
      if (!block.terminator) {
        report("function path falls through without returning", block.range);
        continue;
      }
      const detail::TerminatorSyntax& terminator = *block.terminator;
      const auto target = [&](std::size_t index) -> std::optional<Block> {
        if (index >= terminator.successors.size()) {
          return std::nullopt;
        }
        const auto found = blocks_.find(terminator.successors[index].target);
        if (found == blocks_.end()) {
          report("unknown successor block '" +
                     terminator.successors[index].target + "'",
                 terminator.successors[index].range);
          return std::nullopt;
        }
        return found->second;
      };
      for (const Path& active : current.next) {
        restore(active);
        if (terminator.kind == detail::TerminatorSyntax::Kind::Return) {
          instantiate_return(terminator.values, terminator.range, active.block);
          continue;
        }
        std::vector<std::vector<Value>> edge_arguments;
        for (const detail::SuccessorSyntax& successor : terminator.successors) {
          std::vector<Value> values;
          for (const detail::ExpressionSyntax& argument : successor.arguments) {
            if (auto value = use(argument)) {
              values.push_back(*value);
            }
          }
          edge_arguments.push_back(std::move(values));
        }
        if (terminator.kind == detail::TerminatorSyntax::Kind::Jump) {
          if (auto destination = target(0)) {
            edit_->jump(active.block, *destination,
                        edge_arguments.empty() ? std::vector<Value>{}
                                               : std::move(edge_arguments[0]));
          }
        } else {
          auto condition = terminator.condition ? use(*terminator.condition)
                                                : std::optional<Value>{};
          auto true_target = target(0);
          auto false_target = target(1);
          if (!condition || !true_target || !false_target ||
              edge_arguments.size() != 2U) {
            continue;
          }
          edit_->branch(active.block, *condition, *true_target,
                        std::move(edge_arguments[0]), *false_target,
                        std::move(edge_arguments[1]));
        }
      }
    }
    if (!ok() ||
        !detail::FunctionAccess::commit(*edit_, compiler_, diagnostics_)) {
      return std::nullopt;
    }
    edit_.reset();
    return compiler_.verify(*function_) ? std::move(function_) : std::nullopt;
  }

private:
  std::optional<Function> instantiate_inline() {
    if (body_.blocks.size() != 1U ||
        body_.blocks.front().name != "entry" ||
        body_.blocks.front().terminator) {
      report("an inline function must have one structured entry body",
             body_.range);
      return std::nullopt;
    }
    function_ = compiler_.create_function();
    if (!function_) {
      return std::nullopt;
    }
    std::vector<Type> argument_types;
    argument_types.reserve(inline_arguments_.size());
    for (const auto& [name, type] : inline_arguments_) {
      static_cast<void>(name);
      argument_types.push_back(type);
    }
    if (inline_results_) {
      result_types_ = *inline_results_;
      detail::FunctionAccess::define(*function_, argument_types,
                                     result_types_);
    }
    edit_.emplace(function_->edit());
    locals_.push();
    for (std::size_t index = 0; index < inline_arguments_.size(); ++index) {
      define(inline_arguments_[index].first,
             edit_->argument(argument_types[index]), body_.range);
    }
    blocks_.emplace("entry", function_->entry());
    if (!inline_results_) {
      const auto& statements = body_.blocks.front().statements;
      if (statements.size() != 1U ||
          statements.front().kind != detail::StatementSyntax::Kind::Return ||
          statements.front().values.size() != 1U) {
        report("an inferred inline function must return one expression",
               body_.range);
        return std::nullopt;
      }
      const auto& returned = statements.front().values.front();
      auto [tail, value] = instantiate_expression(
          returned.value, returned.range, function_->entry());
      if (!value) {
        return std::nullopt;
      }
      edit_->ret(tail, {*value});
    } else {
      Flow flow = instantiate_sequence(body_.blocks.front().statements,
                                       function_->entry());
      if (!flow.next.empty()) {
        report("inline function path falls through without returning",
               body_.range);
      }
      if (!flow.breaks.empty() || !flow.continues.empty()) {
        report("loop control escaped an inline function", body_.range);
      }
    }
    if (!ok() ||
        !detail::FunctionAccess::commit(*edit_, compiler_, diagnostics_)) {
      return std::nullopt;
    }
    edit_.reset();
    return compiler_.verify(*function_) ? std::move(function_) : std::nullopt;
  }

  Path path(Block block) const {
    return {std::move(block), locals_, residual_control_depth_};
  }

  Flow next(Block block) const {
    Flow flow;
    flow.next.push_back(path(std::move(block)));
    return flow;
  }

  Flow transfer(bool is_break, Block block) const {
    Flow flow;
    auto& paths = is_break ? flow.breaks : flow.continues;
    paths.push_back(path(std::move(block)));
    return flow;
  }

  void restore(const Path& active) {
    locals_ = active.locals;
    residual_control_depth_ = active.residual_depth;
  }

  static void trim_scopes(Flow& flow, std::size_t depth) {
    const auto trim = [&](std::vector<Path>& paths) {
      for (Path& path : paths) {
        path.locals.resize(depth);
      }
    };
    trim(flow.next);
    trim(flow.breaks);
    trim(flow.continues);
  }

  bool equivalent_staged_value(const detail::StagedValue& lhs,
                               const detail::StagedValue& rhs) const {
    if (detail::same_staged_value(lhs, rhs)) {
      return true;
    }
    if (lhs.known() || rhs.known()) {
      return false;
    }
    const Value* left_value = lhs.residual_value();
    const Value* right_value = rhs.residual_value();
    if (left_value == nullptr || right_value == nullptr) {
      return false;
    }
    const auto left = left_value->defining_op();
    const auto right = right_value->defining_op();
    if (!left || !right ||
        left->callee().symbol() != right->callee().symbol()) {
      return false;
    }
    const auto callee = left->callee();
    if (callee.name() != "literal" ||
        detail::compiler_inputs(callee).size() != 1U ||
        !detail::value_inputs(callee).empty() ||
        !detail::compiler_results(callee).empty() ||
        detail::value_results(callee).size() != 1U) {
      return false;
    }
    const auto left_results = left->results();
    const auto right_results = right->results();
    const auto left_result =
        std::find(left_results.begin(), left_results.end(), *left_value);
    const auto right_result =
        std::find(right_results.begin(), right_results.end(), *right_value);
    if (left_result == left_results.end() ||
        right_result == right_results.end() ||
        std::distance(left_results.begin(), left_result) !=
            std::distance(right_results.begin(), right_result)) {
      return false;
    }
    const auto left_arguments = left->arguments();
    const auto right_arguments = right->arguments();
    return left_arguments.size() == right_arguments.size() &&
           std::equal(left_arguments.begin(), left_arguments.end(),
                      right_arguments.begin());
  }

  bool same_staged_state(std::span<const detail::StagedValue> lhs,
                         std::span<const detail::StagedValue> rhs) const {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [&](const detail::StagedValue& left,
                          const detail::StagedValue& right) {
                        return equivalent_staged_value(left, right);
                      });
  }

  Flow instantiate_sequence(std::span<const detail::StatementSyntax> statements,
                            Block block) {
    Flow current = next(std::move(block));
    for (const detail::StatementSyntax& statement : statements) {
      Flow following;
      following.breaks = std::move(current.breaks);
      following.continues = std::move(current.continues);
      for (const Path& active : current.next) {
        restore(active);
        following.append(instantiate_statement(statement, active.block));
      }
      current = std::move(following);
      if (current.next.empty()) {
        break;
      }
    }
    return current;
  }

  bool ok() const { return diagnostics_.size() == initial_diagnostics_; }

  SourceRange source(detail::SyntaxRange range) const {
    return {body_.source, range.begin, range.end};
  }

  void report(std::string message, detail::SyntaxRange range) {
    diagnostics_.report(std::move(message), source(range));
  }

  std::optional<std::string_view>
  resolve_prefix(std::string_view from, std::string_view prefix) const {
    if (prefix == detail::prelude_module_name) {
      return detail::prelude_module_name;
    }
    if (prefix == from) {
      return from;
    }
    const auto owner = compiler_.module(from);
    if (!owner) {
      return std::nullopt;
    }
    const auto found =
        std::find_if(owner->imports().begin(), owner->imports().end(),
                     [&](const Module::Import& import) {
                       return import.prefix() == prefix;
                     });
    return found == owner->imports().end()
               ? std::nullopt
               : std::optional<std::string_view>{found->name};
  }

  template <typename Declaration>
  std::optional<Declaration> declaration(std::string_view reference,
                                         detail::SyntaxRange range,
                                         std::string_view scope = {}) {
    if (scope.empty()) {
      scope = owner_;
    }
    const bool prelude_type = std::is_same_v<Declaration, Module::TypeDecl> &&
                              detail::is_prelude_type(reference);
    const std::string qualified =
        prelude_type ? std::string(detail::prelude_module_name) + "." +
                           std::string(reference)
                     : std::string(reference);
    const auto [prefix, local] = split_reference(scope, qualified);
    const auto module_name = resolve_prefix(scope, prefix);
    if (!module_name) {
      report("reference '" + std::string(reference) +
                 "' is not local or directly imported",
             range);
      return std::nullopt;
    }
    const auto module = compiler_.module(*module_name);
    if (!module) {
      report("unknown module '" + std::string(*module_name) + "'", range);
      return std::nullopt;
    }
    std::optional<Declaration> result;
    if constexpr (std::is_same_v<Declaration, Module::TypeDecl>) {
      result = module->type(local);
    } else {
      result = module->function(local);
    }
    if (!result) {
      constexpr std::string_view kind =
          std::is_same_v<Declaration, Module::TypeDecl> ? "type" : "function";
      report("unknown " + std::string(kind) + " '" + std::string(reference) +
                 "'",
             range);
    }
    return result;
  }

  std::optional<Module::SymbolKind>
  declaration_kind(std::string_view reference) const {
    const bool prelude_type = detail::is_prelude_type(reference);
    const std::string qualified =
        prelude_type ? std::string(detail::prelude_module_name) + "." +
                           std::string(reference)
                     : std::string(reference);
    const auto [prefix, local] = split_reference(owner_, qualified);
    const auto module_name = resolve_prefix(owner_, prefix);
    const auto module =
        module_name ? compiler_.module(*module_name) : std::optional<Module>{};
    if (!module) {
      return std::nullopt;
    }
    if (module->type(local)) {
      return Module::SymbolKind::Type;
    }
    return std::nullopt;
  }

  std::optional<Module::ParameterDecl>
  known_result(const Module::Expression& expression,
               detail::SyntaxRange range) {
    using Kind = Module::Expression::Kind;
    if (expression.kind == Kind::Evaluate) {
      return expression.arguments.size() == 1U
                 ? known_result(expression.arguments.front(), range)
                 : std::nullopt;
    }
    if (expression.kind == Kind::Variable ||
        expression.kind == Kind::Reference) {
      if (!expression.arguments.empty()) {
        const auto kind = declaration_kind(expression.text);
        if (kind == Module::SymbolKind::Type) {
          return Module::ParameterDecl{
              "result", detail::domain_expression(detail::ValueKind::Type),
              false, std::nullopt};
        }
      }
      auto value = lookup_staged(expression.text);
      auto value_domain =
          value ? detail::type_domain(value->type()) : std::nullopt;
      return value_domain
                 ? std::optional<Module::ParameterDecl>{{"result",
                                                         std::move(
                                                             *value_domain),
                                                         false, std::nullopt}}
                 : std::nullopt;
    }
    if (expression.kind == Kind::Number) {
      const bool real =
          expression.text.find_first_of(".eE") != std::string::npos;
      return Module::ParameterDecl{
          "result",
          detail::domain_expression(real ? detail::ValueKind::Real
                                         : detail::ValueKind::Integer),
          false, std::nullopt};
    }
    if (expression.kind == Kind::Boolean) {
      return Module::ParameterDecl{
          "result", detail::domain_expression(detail::ValueKind::Boolean),
          false, std::nullopt};
    }
    if (expression.kind == Kind::String) {
      return Module::ParameterDecl{
          "result", detail::domain_expression(detail::ValueKind::String), false,
          std::nullopt};
    }
    if (expression.kind == Kind::FunctionType) {
      return Module::ParameterDecl{
          "result", detail::domain_expression(detail::ValueKind::Type), false,
          std::nullopt};
    }
    if (expression.kind == Kind::List && !expression.arguments.empty()) {
      auto element = known_result(expression.arguments.front(), range);
      return element
                 ? std::optional<
                       Module::ParameterDecl>{{"result",
                                               Module::Expression::list_domain(
                                                   element->domain),
                                               false, std::nullopt}}
                 : std::nullopt;
    }
    if (expression.kind == Kind::If && expression.arguments.size() == 3U) {
      return known_result(expression.arguments[1], range);
    }
    if (expression.kind == Kind::Call) {
      std::vector<Module::ParameterDecl> matches;
      for (const auto& function : visible_functions(expression.text)) {
        auto candidate = detail::call_candidate(function, expression);
        const auto results = detail::compiler_results(function);
        if (!candidate || !detail::value_inputs(function).empty() ||
            !detail::value_results(function).empty() || results.size() != 1U) {
          continue;
        }
        bool accepts = true;
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          const auto actual = known_result(expression.arguments[index], range);
          if (actual &&
              function.inputs()[candidate->parameters[index]].domain !=
                  actual->domain) {
            accepts = false;
            break;
          }
        }
        if (accepts) {
          matches.push_back(results.front());
        }
      }
      if (matches.empty()) {
        return std::nullopt;
      }
      return std::all_of(matches.begin() + 1, matches.end(),
                         [&](const Module::ParameterDecl& result) {
                           return result.domain == matches.front().domain;
                         })
                 ? std::optional<Module::ParameterDecl>{matches.front()}
                 : std::nullopt;
    }
    if ((expression.kind == Kind::Prefix || expression.kind == Kind::Infix ||
         expression.kind == Kind::Postfix) &&
        !expression.arguments.empty()) {
      const auto fixity = expression.kind == Kind::Prefix
                              ? Module::FunctionDecl::Fixity::Prefix
                          : expression.kind == Kind::Postfix
                              ? Module::FunctionDecl::Fixity::Postfix
                              : Module::FunctionDecl::Fixity::Infix;
      std::vector<Module::ParameterDecl> matches;
      for (const auto& function : detail::visible_operators(
               compiler_, owner_, expression.text, fixity)) {
        const auto inputs = detail::compiler_inputs(function);
        const auto results = detail::compiler_results(function);
        if (!detail::value_inputs(function).empty() ||
            !detail::value_results(function).empty() ||
            inputs.size() != expression.arguments.size() ||
            results.size() != 1U) {
          continue;
        }
        bool accepts = true;
        for (std::size_t index = 0; index < inputs.size(); ++index) {
          const auto actual = known_result(expression.arguments[index], range);
          if (!actual || inputs[index].domain != actual->domain) {
            accepts = false;
            break;
          }
        }
        if (accepts) {
          matches.push_back(results.front());
        }
      }
      if (!matches.empty() &&
          std::all_of(matches.begin() + 1, matches.end(),
                      [&](const Module::ParameterDecl& result) {
                        return result.domain == matches.front().domain;
                      })) {
        return matches.front();
      }
      return known_result(expression.arguments.front(), range);
    }
    return std::nullopt;
  }

  std::optional<detail::StagedValue> evaluate_known(
      const detail::ExpressionSyntax& syntax,
      std::optional<Module::ParameterDecl> contextual = std::nullopt) {
    auto expected = contextual ? std::move(contextual)
                               : known_result(syntax.value, syntax.range);
    if (!expected) {
      report("cannot determine the type required by compile-time evaluation",
             syntax.range);
      return std::nullopt;
    }
    auto payload = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, *expected, locals_.known_bindings(),
        diagnostics_, source(syntax.range), residual_control_depth_ == 0U);
    auto value = payload ? detail::execution_value(*payload, *expected)
                         : std::optional<detail::ExecutionValue>{};
    return value ? detail::stage(compiler_, std::move(*value))
                 : std::optional<detail::StagedValue>{};
  }

  std::optional<Type> type(const detail::ExpressionSyntax& syntax) {
    const Module::ParameterDecl expected{
        "type", detail::domain_expression(detail::ValueKind::Type), false,
        std::nullopt};
    const std::size_t before = diagnostics_.size();
    auto value = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, expected, locals_.known_bindings(),
        diagnostics_, source(syntax.range), residual_control_depth_ == 0U);
    const Type* resolved = value ? value->as_type() : nullptr;
    if (resolved == nullptr && diagnostics_.size() == before) {
      report("type annotation does not evaluate to a Known type", syntax.range);
    }
    return resolved ? std::optional<Type>{*resolved} : std::nullopt;
  }

  std::optional<Value> lookup(std::string_view name) const {
    auto value = lookup_staged(name);
    return value ? detail::ir_value(compiler_, *value) : std::optional<Value>{};
  }

  std::optional<detail::StagedValue>
  lookup_staged(std::string_view name) const {
    const detail::StagedValue* value = locals_.find(name);
    return value ? std::optional<detail::StagedValue>{*value} : std::nullopt;
  }

  bool declared_local(std::string_view name) const {
    return locals_.contains(name);
  }

  std::vector<Module::FunctionDecl>
  visible_functions(std::string_view reference) const {
    return detail::visible_functions(compiler_, owner_, reference);
  }

  std::optional<PendingCall>
  plan_call(const Module::FunctionDecl& function,
            const Module::Expression& expression,
            std::span<const PendingArgument> supplied,
            std::span<const std::optional<Type>> expected,
            detail::SyntaxRange range, bool allow_guarded_evaluation,
            Diagnostics* errors = nullptr) {
    const auto reject = [&](std::string message) {
      if (errors) {
        errors->report(std::move(message), source(range));
      }
    };
    const auto candidate = detail::call_candidate(function, expression);
    if (!candidate || candidate->parameters.size() != supplied.size()) {
      reject("call arguments do not match '" + function.signature() + "'");
      return std::nullopt;
    }

    const auto parameters = function.inputs();
    PendingCall result{
        function,
        std::vector<std::vector<PendingArgument>>(parameters.size()),
        {},
        std::vector<std::optional<ParameterValue>>(
            detail::compiler_inputs(function).size())};
    for (std::size_t index = 0; index < supplied.size(); ++index) {
      result.arguments[candidate->parameters[index]].push_back(supplied[index]);
    }

    std::vector<std::optional<Type>> argument_types;
    std::size_t known_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      auto& arguments = result.arguments[index];
      if (arguments.empty() && parameters[index].default_value) {
        const auto payload = detail::parameter_default(parameters[index]);
        auto execution =
            payload ? detail::execution_value(*payload, parameters[index])
                    : std::optional<detail::ExecutionValue>{};
        auto value = execution ? detail::stage(compiler_, std::move(*execution))
                               : std::optional<detail::StagedValue>{};
        if (!value) {
          reject("cannot construct default argument '" +
                 parameters[index].name + "'");
          return std::nullopt;
        }
        arguments.push_back(
            {std::move(*value), {}, std::nullopt, std::nullopt});
      }
      if (arguments.empty() && !parameters[index].variadic) {
        reject("call is missing argument '" + parameters[index].name + "'");
        return std::nullopt;
      }
      if (detail::is_value_port(parameters[index])) {
        for (const PendingArgument& argument : arguments) {
          argument_types.push_back(
              argument.value ? std::optional<Type>{argument.value->type()}
                             : std::nullopt);
        }
        continue;
      }
      if (arguments.size() != 1U || !arguments.front().value ||
          !arguments.front().value->known()) {
        reject("argument '" + parameters[index].name +
               "' must be one Known value");
        return std::nullopt;
      }
      const detail::ExecutionValue* known =
          arguments.front().value->known_value();
      const auto payload = known ? detail::parameter_value(*known)
                                 : std::optional<ParameterValue>{};
      if (!payload || !detail::matches_parameter(parameters[index], *payload)) {
        reject("argument '" + parameters[index].name +
               "' has an incompatible compiler domain");
        return std::nullopt;
      }
      result.known_arguments[known_index++] = *payload;
    }

    Diagnostics attempt;
    auto types = detail::resolve_partial_call_types(
        compiler_, function, argument_types, result.known_arguments, expected,
        errors ? *errors : attempt, source(range), allow_guarded_evaluation);
    if (!types || types->arguments.size() != argument_types.size()) {
      return std::nullopt;
    }
    std::size_t value_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (!detail::is_value_port(parameters[index])) {
        continue;
      }
      for (const PendingArgument& argument : result.arguments[index]) {
        if (argument.is_inline_function()) {
          const Type& callable = types->arguments[value_index];
          const Module::Symbol schema = callable.schema().symbol();
          const auto inputs = callable.get<std::vector<Type>>("inputs");
          const auto results = callable.get<std::vector<Type>>("results");
          if (schema.module_name() != detail::prelude_module_name ||
              schema.local_name() != "callable" || !inputs || !results ||
              results->size() != 1U || !argument.inline_inputs ||
              *inputs != *argument.inline_inputs) {
            reject("inline function does not match argument '" +
                   parameters[index].name + "'");
            return std::nullopt;
          }
        }
        ++value_index;
      }
    }
    result.partial_types = std::move(*types);
    return result;
  }

  bool matches_function_value(const Module::FunctionDecl& function,
                              const Type& callable, detail::SyntaxRange range) {
    const Module::Symbol schema = callable.schema().symbol();
    const auto inputs = callable.get<std::vector<Type>>("inputs");
    const auto results = callable.get<std::vector<Type>>("results");
    if (schema.module_name() != detail::prelude_module_name ||
        schema.local_name() != "callable" || !inputs || !results ||
        !detail::compiler_inputs(function).empty() ||
        !detail::compiler_results(function).empty()) {
      return false;
    }
    std::vector<std::optional<Type>> expected;
    expected.reserve(results->size());
    for (const Type& result : *results) {
      expected.emplace_back(result);
    }
    Diagnostics attempt;
    const auto resolved = detail::resolve_call_types(
        compiler_, function, *inputs, {}, expected, attempt, source(range));
    return resolved && resolved->results == *results;
  }

  std::optional<Value>
  function_reference(std::string_view reference, detail::SyntaxRange range,
                     std::optional<Type> expected_type = std::nullopt) {
    const auto overloads = visible_functions(reference);
    if (overloads.empty()) {
      return std::nullopt;
    }
    if (expected_type) {
      std::vector<Module::FunctionDecl> matches;
      for (const auto& overload : overloads) {
        if (matches_function_value(overload, *expected_type, range)) {
          matches.push_back(overload);
        }
      }
      if (matches.empty()) {
        report("no overload of function '" + std::string(reference) +
                   "' matches the contextual callable type",
               range);
        return std::nullopt;
      }
      if (matches.size() != 1U) {
        report("function value '" + std::string(reference) +
                   "' remains ambiguous for the contextual callable type",
               range);
        return std::nullopt;
      }
      return edit_->reference(matches.front(), std::move(*expected_type));
    }
    if (overloads.size() != 1U) {
      report("overloaded function '" + std::string(reference) +
                 "' needs a contextual callable type",
             range);
      return std::nullopt;
    }
    const Module::FunctionDecl declaration = overloads.front();
    if (!declaration.generics().empty()) {
      report("generic function '" + std::string(reference) +
                 "' needs a contextual callable type",
             range);
      return std::nullopt;
    }
    if (!detail::compiler_inputs(declaration).empty() ||
        !detail::compiler_results(declaration).empty()) {
      report("function value '" + std::string(reference) +
                 "' requires compile-time specialization",
             range);
      return std::nullopt;
    }

    const Module::ParameterDecl expected{
        "function value type",
        detail::domain_expression(detail::ValueKind::Type), false,
        std::nullopt};
    const detail::KnownBindings bindings;
    const auto resolve_ports =
        [&](const auto& ports) -> std::optional<std::vector<Type>> {
      std::vector<Type> types;
      types.reserve(ports.size());
      for (const auto& port : ports) {
        auto value = detail::evaluate_known_expression(
            compiler_, declaration.symbol().module_name(), port.domain,
            expected, bindings, diagnostics_, source(range),
            residual_control_depth_ == 0U);
        const Type* type = value ? value->as_type() : nullptr;
        if (type == nullptr) {
          return std::nullopt;
        }
        types.push_back(*type);
      }
      return types;
    };
    auto inputs = resolve_ports(detail::value_inputs(declaration));
    auto results = resolve_ports(detail::value_results(declaration));
    const auto prelude = compiler_.module(detail::prelude_module_name);
    const auto callable =
        prelude ? prelude->type("callable") : std::optional<Module::TypeDecl>{};
    auto type = inputs && results && callable
                    ? compiler_.make(*callable, *inputs, *results)
                    : std::optional<Type>{};
    if (!type) {
      report("cannot construct callable type for function '" +
                 std::string(reference) + "'",
             range);
      return std::nullopt;
    }
    return edit_->reference(declaration, std::move(*type));
  }

  std::optional<Function> build_inline_function(
      const Module::Expression& expression,
      const std::vector<Type>* expected_inputs,
      std::optional<std::vector<Type>> expected_results,
      detail::SyntaxRange range) {
    return detail::instantiate_lambda(
        compiler_, owner_, expression, source(range), diagnostics_,
        locals_.known_bindings(),
        expected_inputs
            ? std::optional<std::vector<Type>>{*expected_inputs}
            : std::nullopt,
        std::move(expected_results), residual_control_depth_ == 0U);
  }

  std::optional<Value> inline_function(const Module::Expression& expression,
                                       const Type& callable,
                                       detail::SyntaxRange range) {
    const Module::Symbol schema = callable.schema().symbol();
    const auto inputs = callable.get<std::vector<Type>>("inputs");
    const auto results = callable.get<std::vector<Type>>("results");
    if (schema.module_name() != detail::prelude_module_name ||
        schema.local_name() != "callable" || !inputs || !results ||
        results->size() != 1U) {
      report("inline function does not match its callable context", range);
      return std::nullopt;
    }
    auto function = build_inline_function(expression, &*inputs, *results, range);
    if (!function) {
      return std::nullopt;
    }
    return edit_->callable(std::move(*function), callable);
  }

  std::optional<detail::ExecutionValue>
  compiler_function(const Module::Expression& expression,
                    detail::SyntaxRange range) {
    auto function = build_inline_function(expression, nullptr, std::nullopt,
                                          range);
    return function
               ? std::optional<detail::ExecutionValue>{
                     detail::store_execution_value(std::move(*function))}
               : std::nullopt;
  }

  std::optional<Value> use(const detail::LocalUseSyntax& use) {
    if (auto value = lookup(use.name)) {
      return value;
    }
    report("use of undefined local value '" + use.name + "'", use.range);
    return std::nullopt;
  }

  std::optional<Value> use(const detail::ExpressionSyntax& expression,
                           std::optional<Type> expected_type = std::nullopt) {
    if ((expression.value.kind != Module::Expression::Kind::Reference &&
         expression.value.kind != Module::Expression::Kind::Variable) ||
        !expression.value.arguments.empty()) {
      report("expected a local value reference", expression.range);
      return std::nullopt;
    }
    if (auto value = lookup(expression.value.text)) {
      return value;
    }
    if (declared_local(expression.value.text)) {
      return std::nullopt;
    }
    if (expression.value.kind == Module::Expression::Kind::Reference) {
      if (auto value =
              function_reference(expression.value.text, expression.range,
                                 std::move(expected_type))) {
        return value;
      }
      if (!visible_functions(expression.value.text).empty()) {
        return std::nullopt;
      }
    }
    report("use of undefined local value '" + expression.value.text + "'",
           expression.range);
    return std::nullopt;
  }

  void define(std::string name, Value value, detail::SyntaxRange range) {
    auto staged = detail::stage(std::move(value));
    if (!staged) {
      report("a local value cannot enter staged evaluation", range);
      return;
    }
    define_staged(std::move(name), std::move(*staged), range);
  }

  void define_staged(std::string name, detail::StagedValue value,
                     detail::SyntaxRange range) {
    if (!locals_.define(std::move(name), std::move(value))) {
      report("a local value may only be defined once", range);
    }
  }

  void bind(const detail::BindingSyntax& binding, std::optional<Value> value) {
    std::optional<detail::StagedValue> staged;
    if (value) {
      staged = detail::stage(std::move(*value));
      if (!staged) {
        report("a local value cannot enter staged evaluation", binding.range);
        return;
      }
    }
    bind_staged(binding, std::move(staged));
  }

  void bind_staged(const detail::BindingSyntax& binding,
                   std::optional<detail::StagedValue> value) {
    if (!binding.rebind) {
      if (!locals_.define(binding.name, std::move(value))) {
        report("a local value may only be defined once", binding.range);
      }
      return;
    }
    if (locals_.assign(binding.name, std::move(value))) {
      return;
    }
    report("cannot rebind undefined local value '" + binding.name + "'",
           binding.range);
  }

  void invalidate(std::span<const detail::BindingSyntax> bindings) {
    for (const auto& binding : bindings) {
      bind(binding, std::nullopt);
    }
  }

  std::optional<Type> expected_type(const detail::BindingSyntax& binding) {
    if (binding.type) {
      return type(*binding.type);
    }
    const auto expected = expected_values_.find(binding.name);
    return expected == expected_values_.end()
               ? std::optional<Type>{}
               : std::optional<Type>{expected->second};
  }

  std::optional<Value> materialize(Value value, Type target, Block block,
                                   detail::SyntaxRange range) {
    if (!value.known()) {
      if (value.type() != target) {
        report("Residual value has the wrong materialized type", range);
        return std::nullopt;
      }
      return value;
    }
    const auto payload = detail::FunctionAccess::known_value(value);
    if (!payload) {
      report("no literal function is available for this Known value", range);
      return std::nullopt;
    }

    std::vector<Module> visible;
    std::unordered_set<std::string> seen_modules;
    const auto add_module = [&](std::string_view name) {
      if (!seen_modules.insert(std::string(name)).second) {
        return;
      }
      if (auto module = compiler_.module(name)) {
        visible.push_back(std::move(*module));
      }
    };
    add_module(owner_);
    const auto owner = compiler_.module(owner_);
    if (owner) {
      for (const auto& import : owner->imports()) {
        add_module(import.name);
      }
    }
    add_module(detail::prelude_module_name);

    std::vector<Module::FunctionDecl> matches;
    for (const Module& module : visible) {
      for (const auto& candidate : module.functions()) {
        if (candidate.name() != "literal" ||
            detail::compiler_inputs(candidate).size() != 1U ||
            !detail::value_inputs(candidate).empty() ||
            !detail::compiler_results(candidate).empty() ||
            detail::value_results(candidate).size() != 1U) {
          continue;
        }
        Diagnostics candidate_diagnostics;
        const std::array<std::optional<ParameterValue>, 1> known{payload};
        const std::array<std::optional<Type>, 1> expected{target};
        if (detail::resolve_call_types(compiler_, candidate, {}, known,
                                       expected, candidate_diagnostics)) {
          matches.push_back(candidate);
        }
      }
    }
    if (matches.empty()) {
      report("no visible literal function can materialize Known value as '" +
                 std::string(target.schema().name()) + "'",
             range);
      return std::nullopt;
    }
    if (matches.size() != 1U) {
      std::string message =
          "more than one visible literal function can materialize this value:";
      for (const auto& candidate : matches) {
        message += " '" + candidate.symbol().qualified_name() + "'";
      }
      report(std::move(message), range);
      return std::nullopt;
    }

    Op op =
        edit_->append(block, matches.front(), {value}, {target});
    detail::FunctionAccess::locate(*edit_, op, source(range));
    return op.result(0);
  }

  std::pair<Block, std::optional<Value>>
  instantiate_expression(const Module::Expression& expression,
                         detail::SyntaxRange range, Block block) {
    using Kind = Module::Expression::Kind;
    const detail::ExpressionSyntax syntax{expression, range};
    if ((expression.kind == Kind::Variable ||
         expression.kind == Kind::Reference) &&
        expression.arguments.empty()) {
      const auto expected = expected_values_.find(expression.text);
      return {block, use(syntax, expected == expected_values_.end()
                                     ? std::optional<Type>{}
                                     : std::optional<Type>{expected->second})};
    }
    const std::string name = "$value" + std::to_string(next_temporary_++);
    detail::StatementSyntax statement;
    statement.bindings.push_back({name, std::nullopt, range});
    statement.expression = syntax;
    statement.range = range;
    Flow flow = instantiate_statement(statement, block);
    if (flow.next.size() != 1U || !flow.breaks.empty() ||
        !flow.continues.empty()) {
      report("expression produced non-local control flow", range);
      return {block, std::nullopt};
    }
    restore(flow.next.front());
    return {flow.next.front().block, use(detail::LocalUseSyntax{name, range})};
  }

  void
  collect_rebindings(const std::vector<detail::StatementSyntax>& statements,
                     std::vector<std::string>& names,
                     std::unordered_set<std::string>& seen) const {
    for (const auto& statement : statements) {
      if (statement.kind != detail::StatementSyntax::Kind::Expression) {
        collect_rebindings(statement.body, names, seen);
        if (statement.kind == detail::StatementSyntax::Kind::If) {
          collect_rebindings(statement.otherwise, names, seen);
        }
        continue;
      }
      for (const auto& binding : statement.bindings) {
        if (binding.rebind && lookup(binding.name) &&
            seen.insert(binding.name).second) {
          names.push_back(binding.name);
        }
      }
    }
  }

  void collect_effect_state(std::vector<std::string>& names,
                            std::unordered_set<std::string>& seen) const {
    for (const std::string& name : locals_.names()) {
      const auto value = lookup_staged(name);
      if (value && !value->known() && detail::is_effect_type(value->type()) &&
          seen.insert(name).second) {
        names.push_back(name);
      }
    }
  }

  Flow instantiate_if_statement(const detail::StatementSyntax& statement,
                                Block block) {
    if (known_result(statement.expression.value, statement.expression.range)) {
      auto condition = evaluate_known(statement.expression);
      const auto selected =
          condition ? detail::known_boolean(*condition) : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               statement.expression.range);
        return next(block);
      }
      const auto& arm = *selected ? statement.body : statement.otherwise;
      const std::size_t outer_depth = locals_.depth();
      locals_.push();
      Flow flow = instantiate_sequence(arm, block);
      trim_scopes(flow, outer_depth);
      return flow;
    }

    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);
    collect_rebindings(statement.otherwise, carried_names, seen);
    collect_effect_state(carried_names, seen);

    auto [condition_tail, condition] = instantiate_expression(
        statement.expression.value, statement.expression.range, block);
    const auto i1 = compiler_.make("i1");
    if (!condition || condition->known() || !i1 || condition->type() != *i1) {
      report("Residual if condition must have type i1",
             statement.expression.range);
      return next(block);
    }
    std::vector<std::size_t> effect_carried;
    std::vector<Type> effect_types;
    std::vector<Value> effect_values;
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      const auto value = lookup_staged(carried_names[index]);
      if (!value || value->known() ||
          !detail::is_effect_type(value->type())) {
        continue;
      }
      const auto residual = value->residual_value();
      if (residual == nullptr) {
        continue;
      }
      effect_carried.push_back(index);
      effect_types.push_back(value->type());
      effect_values.push_back(*residual);
    }

    const Block yes = edit_->block(effect_types);
    const Block no = edit_->block(effect_types);
    edit_->branch(condition_tail, *condition, yes, effect_values, no,
                  effect_values);

    const detail::Locals incoming = locals_;
    const auto elaborate = [&](const std::vector<detail::StatementSyntax>& arm,
                               Block start) {
      locals_ = incoming;
      locals_.push();
      const auto arguments = start.arguments();
      for (std::size_t index = 0; index < effect_carried.size(); ++index) {
        define(carried_names[effect_carried[index]], arguments[index],
               statement.range);
      }
      Flow flow = instantiate_sequence(arm, start);
      std::vector<std::optional<detail::StagedValue>> values;
      values.reserve(carried_names.size());
      if (flow.next.size() == 1U) {
        restore(flow.next.front());
        for (const std::string& name : carried_names) {
          values.push_back(lookup_staged(name));
        }
      }
      trim_scopes(flow, incoming.depth());
      return std::pair{flow, std::move(values)};
    };

    ++residual_control_depth_;
    auto [true_flow, true_values] = elaborate(statement.body, yes);
    auto [false_flow, false_values] = elaborate(statement.otherwise, no);
    --residual_control_depth_;
    locals_ = incoming;

    const bool transfers =
        !true_flow.breaks.empty() || !true_flow.continues.empty() ||
        !false_flow.breaks.empty() || !false_flow.continues.empty();
    if (transfers) {
      Flow result;
      result.append(std::move(true_flow));
      result.append(std::move(false_flow));
      return result;
    }
    const bool true_continues = true_flow.next.size() == 1U;
    const bool false_continues = false_flow.next.size() == 1U;
    if (!true_continues && !false_continues) {
      return {};
    }
    if (!true_continues || !false_continues) {
      const auto& values = true_continues ? true_values : false_values;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        if (!values[index]) {
          report("surviving if arm does not produce outer binding '" +
                     carried_names[index] + "'",
                 statement.range);
          continue;
        }
        bind_staged({carried_names[index], std::nullopt, statement.range, true},
                    values[index]);
      }
      return true_continues ? std::move(true_flow) : std::move(false_flow);
    }

    std::vector<Type> merge_types;
    std::vector<Value> true_arguments;
    std::vector<Value> false_arguments;
    std::vector<std::optional<detail::StagedValue>> unchanged(
        carried_names.size());
    std::vector<std::optional<std::size_t>> merged(carried_names.size());
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      if (!true_values[index] || !false_values[index]) {
        report("if arm does not produce outer binding '" +
                   carried_names[index] + "'",
               statement.range);
        continue;
      }
      if (detail::same_staged_value(*true_values[index],
                                    *false_values[index])) {
        unchanged[index] = *true_values[index];
        continue;
      }
      std::optional<Type> target;
      if (!true_values[index]->known()) {
        target = true_values[index]->type();
      } else if (!false_values[index]->known()) {
        target = false_values[index]->type();
      } else if (true_values[index]->type() == false_values[index]->type()) {
        target = true_values[index]->type();
      }
      if (!target ||
          (!true_values[index]->known() &&
           true_values[index]->type() != *target) ||
          (!false_values[index]->known() &&
           false_values[index]->type() != *target)) {
        report("if arms assign incompatible types to '" + carried_names[index] +
                   "'",
               statement.range);
        continue;
      }
      auto true_ir = detail::ir_value(compiler_, *true_values[index]);
      auto false_ir = detail::ir_value(compiler_, *false_values[index]);
      auto true_value =
          true_ir ? materialize(*true_ir, *target, true_flow.next.front().block,
                                statement.range)
                  : std::optional<Value>{};
      auto false_value =
          false_ir ? materialize(*false_ir, *target,
                                 false_flow.next.front().block, statement.range)
                   : std::optional<Value>{};
      if (!true_value || !false_value) {
        continue;
      }
      merged[index] = merge_types.size();
      merge_types.push_back(*target);
      true_arguments.push_back(*true_value);
      false_arguments.push_back(*false_value);
    }
    if (!ok()) {
      return next(block);
    }

    const Block merge = edit_->block(merge_types);
    edit_->jump(true_flow.next.front().block, merge, true_arguments);
    edit_->jump(false_flow.next.front().block, merge, false_arguments);
    const auto arguments = merge.arguments();
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      const detail::BindingSyntax binding{carried_names[index], std::nullopt,
                                          statement.range, true};
      if (merged[index]) {
        bind(binding, arguments[*merged[index]]);
      } else {
        bind_staged(binding, unchanged[index]);
      }
    }
    return next(merge);
  }

  Flow instantiate_while(const detail::StatementSyntax& statement,
                         Block block) {
    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);
    collect_effect_state(carried_names, seen);

    if (known_result(statement.expression.value, statement.expression.range)) {
      struct StagedState {
        std::vector<detail::StagedValue> values;
        Block block;
      };
      std::vector<StagedState> visited;
      std::vector<Path> pending{path(block)};
      Flow exits;
      while (!pending.empty() && ok()) {
        Path active = std::move(pending.back());
        pending.pop_back();
        restore(active);
        auto condition = evaluate_known(statement.expression);
        const auto selected =
            condition ? detail::known_boolean(*condition) : std::nullopt;
        if (!selected) {
          report("Known while condition must have type bool",
                 statement.expression.range);
          continue;
        }
        if (!*selected) {
          exits.next.push_back(path(active.block));
          continue;
        }
        std::vector<detail::StagedValue> state;
        state.reserve(carried_names.size());
        for (const std::string& name : carried_names) {
          if (auto value = lookup_staged(name)) {
            state.push_back(std::move(*value));
          }
        }
        const auto repeated =
            state.size() == carried_names.size()
                ? std::find_if(visited.begin(), visited.end(),
                               [&](const StagedState& previous) {
                                 return same_staged_state(previous.values,
                                                          state);
                               })
                : visited.end();
        if (repeated != visited.end() && active.residual_depth != 0U) {
          edit_->jump(active.block, repeated->block);
          continue;
        }
        if (state.size() == carried_names.size()) {
          visited.push_back({std::move(state), active.block});
        }
        if (loop_iterations_++ >= compiler_.evaluation_limits().steps) {
          report(active.residual_depth == 0U
                     ? "compile-time while iteration limit exceeded"
                     : "mixed-stage while does not reach a finite "
                       "specialization; make its condition and carried "
                       "state Residual",
                 statement.range);
          continue;
        }
        const std::size_t outer_depth = locals_.depth();
        locals_.push();
        loops_.push_back({});
        Flow flow = instantiate_sequence(statement.body, active.block);
        loops_.pop_back();
        trim_scopes(flow, outer_depth);
        exits.next.insert(exits.next.end(),
                          std::make_move_iterator(flow.breaks.begin()),
                          std::make_move_iterator(flow.breaks.end()));
        pending.insert(pending.end(),
                       std::make_move_iterator(flow.next.begin()),
                       std::make_move_iterator(flow.next.end()));
        pending.insert(pending.end(),
                       std::make_move_iterator(flow.continues.begin()),
                       std::make_move_iterator(flow.continues.end()));
      }
      return exits;
    }

    std::vector<Value> initial;
    std::vector<Type> carried_types;
    for (const std::string& name : carried_names) {
      auto value = lookup(name);
      if (!value) {
        report("loop-carried value '" + name + "' is unavailable",
               statement.range);
        continue;
      }
      std::optional<Type> target;
      if (!value->known()) {
        target = value->type();
      } else if (const auto expected = expected_values_.find(name);
                 expected != expected_values_.end()) {
        target = expected->second;
      }
      if (!target) {
        report("Known loop-carried value '" + name +
                   "' needs an explicit or downstream module type",
               statement.range);
        continue;
      }
      carried_types.push_back(*target);
      auto carried = materialize(*value, *target, block, statement.range);
      if (carried) {
        initial.push_back(*carried);
      }
    }
    if (initial.size() != carried_names.size()) {
      return next(block);
    }

    std::vector<std::size_t> effect_carried;
    std::vector<Type> effect_types;
    for (std::size_t index = 0; index < carried_types.size(); ++index) {
      if (detail::is_effect_type(carried_types[index])) {
        effect_carried.push_back(index);
        effect_types.push_back(carried_types[index]);
      }
    }

    const Block header = edit_->block(carried_types);
    const Block body = edit_->block(effect_types);
    const Block exit = edit_->block(carried_types);
    edit_->jump(block, header, initial);

    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      bind({carried_names[index], std::nullopt, statement.range, true},
           header.arguments()[index]);
    }
    auto [condition_tail, condition] = instantiate_expression(
        statement.expression.value, statement.expression.range, header);
    const auto i1 = compiler_.make("i1");
    if (!condition || condition->known() || !i1 || condition->type() != *i1) {
      report("Residual while condition must have type i1",
             statement.expression.range);
      return next(block);
    }
    std::vector<Value> body_arguments;
    body_arguments.reserve(effect_carried.size());
    for (const std::size_t index : effect_carried) {
      body_arguments.push_back(header.arguments()[index]);
    }
    edit_->branch(condition_tail, *condition, body, body_arguments, exit,
                  header.arguments());

    const std::size_t outer_scope_depth = locals_.depth();
    const std::size_t outer_residual_depth = residual_control_depth_;
    ++residual_control_depth_;
    locals_.push();
    for (std::size_t index = 0; index < effect_carried.size(); ++index) {
      define(carried_names[effect_carried[index]], body.arguments()[index],
             statement.range);
    }
    loops_.push_back({header, exit, carried_names, carried_types});
    Flow body_flow = instantiate_sequence(statement.body, body);
    loops_.pop_back();
    for (const Path& active : body_flow.next) {
      restore(active);
      std::vector<Value> updated;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        const std::string& name = carried_names[index];
        if (auto value = lookup(name)) {
          auto carried = materialize(*value, carried_types[index], active.block,
                                     statement.range);
          if (carried) {
            updated.push_back(*carried);
          }
        }
      }
      if (updated.size() != carried_names.size()) {
        report("while body does not produce every loop-carried value",
               statement.range);
        continue;
      }
      edit_->jump(active.block, header, std::move(updated));
    }
    if (!body_flow.breaks.empty() || !body_flow.continues.empty()) {
      report("loop control was not consumed by its Residual loop",
             statement.range);
    }
    locals_.resize(outer_scope_depth);
    residual_control_depth_ = outer_residual_depth;
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      bind({carried_names[index], std::nullopt, statement.range, true},
           exit.arguments()[index]);
    }
    return next(exit);
  }

  struct CountedRange {
    std::int64_t start = 0;
    std::int64_t limit = 0;
    std::int64_t step = 1;
    bool empty = false;
  };

  struct CountedRangeProbe {
    bool matched = false;
    std::optional<CountedRange> value;
  };

  CountedRangeProbe
  prelude_counted_range(const detail::ExpressionSyntax& syntax) {
    using Kind = Module::Expression::Kind;
    const Module::Expression* expression = &syntax.value;
    if (expression->kind == Kind::Evaluate &&
        expression->arguments.size() == 1U) {
      expression = &expression->arguments.front();
    }
    if (expression->kind != Kind::Call) {
      return {};
    }

    std::optional<Module::FunctionDecl> selected;
    std::optional<detail::CallCandidate> selected_call;
    for (const auto& function : visible_functions(expression->text)) {
      const auto candidate = detail::call_candidate(function, *expression);
      if (!candidate || !detail::is_prelude_primitive(function) ||
          function.name() != "range") {
        continue;
      }
      if (selected) {
        return {};
      }
      selected = function;
      selected_call = *candidate;
    }
    if (!selected || !selected_call) {
      return {};
    }

    std::array<std::int64_t, 3> arguments{0, 0, 1};
    for (std::size_t index = 0; index < expression->arguments.size();
         ++index) {
      const std::size_t parameter = selected_call->parameters[index];
      auto value = evaluate_known({expression->arguments[index], syntax.range},
                                  selected->inputs()[parameter]);
      const auto* integer =
          value && value->known()
              ? std::get_if<std::int64_t>(value->known_value())
              : nullptr;
      if (integer == nullptr) {
        return {true, std::nullopt};
      }
      arguments[parameter] = *integer;
    }

    const std::size_t count = expression->arguments.size();
    const std::int64_t start = count == 1U ? 0 : arguments[0];
    const std::int64_t stop = count == 1U ? arguments[0] : arguments[1];
    const std::int64_t step = count == 3U ? arguments[2] : 1;
    if (step == 0) {
      report("range step cannot be zero", syntax.range);
      return {true, std::nullopt};
    }
    if (step > 0 ? start >= stop : start <= stop) {
      return {true, CountedRange{start, stop, step, true}};
    }

    const auto checked_add =
        [](std::int64_t lhs, std::int64_t rhs) -> std::optional<std::int64_t> {
      if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
          (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
        return std::nullopt;
      }
      return lhs + rhs;
    };
    std::int64_t last = start;
    if (step > 0) {
      const std::uint64_t distance =
          static_cast<std::uint64_t>(stop) - static_cast<std::uint64_t>(start);
      const std::uint64_t remainder =
          (distance - 1U) % static_cast<std::uint64_t>(step);
      last = (stop - 1) - static_cast<std::int64_t>(remainder);
    } else {
      const std::uint64_t distance =
          static_cast<std::uint64_t>(start) - static_cast<std::uint64_t>(stop);
      const std::uint64_t magnitude =
          std::uint64_t{0} - static_cast<std::uint64_t>(step);
      const std::uint64_t remainder = (distance - 1U) % magnitude;
      last = (stop + 1) + static_cast<std::int64_t>(remainder);
    }
    const auto limit = checked_add(last, step);
    if (!limit) {
      report("typed for range exceeds the compiler integer domain",
             syntax.range);
      return {true, std::nullopt};
    }
    return {true, CountedRange{start, *limit, step, false}};
  }

  Flow instantiate_for(const detail::StatementSyntax& statement, Block block) {
    if (!statement.iterator) {
      report("for statement has no iterator", statement.range);
      return next(block);
    }
    std::optional<CountedRange> counted;
    std::optional<std::vector<detail::ExecutionValue>> elements;
    if (statement.iterator->type) {
      auto probe = prelude_counted_range(statement.expression);
      if (probe.matched) {
        if (!probe.value) {
          return next(block);
        }
        counted = *probe.value;
      }
    }
    if (!counted) {
      auto iterable = evaluate_known(statement.expression);
      const detail::ExecutionValue* payload =
          iterable && iterable->known() ? iterable->known_value() : nullptr;
      elements = payload ? detail::list_elements(*payload)
                         : std::optional<std::vector<detail::ExecutionValue>>{};
      if (!elements) {
        report("for iterable must be a Known list", statement.expression.range);
        return next(block);
      }
    }

    if (statement.iterator->type) {
      const auto iterator_type = expected_type(*statement.iterator);
      if (!iterator_type) {
        return next(block);
      }
      if ((counted && counted->empty) || (elements && elements->empty())) {
        return next(block);
      }

      std::vector<std::int64_t> sequence;
      if (elements) {
        sequence.reserve(elements->size());
        for (const detail::ExecutionValue& element : *elements) {
          const auto* integer = std::get_if<std::int64_t>(&element);
          if (!integer) {
            report("a typed for iterable must contain integers",
                   statement.expression.range);
            return next(block);
          }
          sequence.push_back(*integer);
        }
      }

      const auto checked_add = [](std::int64_t lhs, std::int64_t rhs)
          -> std::optional<std::int64_t> {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
          return std::nullopt;
        }
        return lhs + rhs;
      };
      const auto checked_subtract = [](std::int64_t lhs, std::int64_t rhs)
          -> std::optional<std::int64_t> {
        if ((rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs) ||
            (rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs)) {
          return std::nullopt;
        }
        return lhs - rhs;
      };

      std::int64_t start_integer = 0;
      std::int64_t limit_integer = 0;
      std::int64_t step = 1;
      if (counted) {
        start_integer = counted->start;
        limit_integer = counted->limit;
        step = counted->step;
      } else {
        start_integer = sequence.front();
        if (sequence.size() > 1U) {
          const auto difference = checked_subtract(sequence[1], sequence[0]);
          if (!difference || *difference == 0) {
            report("a typed for iterable must be an arithmetic progression",
                   statement.expression.range);
            return next(block);
          }
          step = *difference;
          for (std::size_t index = 2; index < sequence.size(); ++index) {
            const auto next_step =
                checked_subtract(sequence[index], sequence[index - 1U]);
            if (!next_step || *next_step != step) {
              report("a typed for iterable must be an arithmetic progression",
                     statement.expression.range);
              return next(block);
            }
          }
        } else if (sequence.front() ==
                   std::numeric_limits<std::int64_t>::max()) {
          step = -1;
        }
        const auto limit = checked_add(sequence.back(), step);
        if (!limit) {
          report("typed for range exceeds the compiler integer domain",
                 statement.expression.range);
          return next(block);
        }
        limit_integer = *limit;
      }

      std::vector<std::string> carried_names;
      std::unordered_set<std::string> seen;
      collect_rebindings(statement.body, carried_names, seen);
      collect_effect_state(carried_names, seen);
      std::vector<Type> carried_types;
      std::vector<Value> initial;
      for (const std::string& name : carried_names) {
        auto value = lookup(name);
        if (!value) {
          report("loop-carried value '" + name + "' is unavailable",
                 statement.range);
          continue;
        }
        std::optional<Type> target;
        if (!value->known()) {
          target = value->type();
        } else if (const auto expected = expected_values_.find(name);
                   expected != expected_values_.end()) {
          target = expected->second;
        }
        if (!target) {
          report("Known loop-carried value '" + name +
                     "' needs an explicit or downstream module type",
                 statement.range);
          continue;
        }
        carried_types.push_back(*target);
        auto carried = materialize(*value, *target, block, statement.range);
        if (carried) {
          initial.push_back(*carried);
        }
      }
      if (initial.size() != carried_names.size()) {
        return next(block);
      }

      const auto materialize_integer = [&](std::int64_t integer) {
        auto staged =
            detail::stage(compiler_, detail::ExecutionValue{integer});
        auto known = staged ? detail::ir_value(compiler_, *staged)
                            : std::optional<Value>{};
        return known ? materialize(*known, *iterator_type, block,
                                   statement.iterator->range)
                     : std::optional<Value>{};
      };
      auto start = materialize_integer(start_integer);
      auto bound = materialize_integer(limit_integer);
      auto step_value = materialize_integer(step);
      if (!start || !bound || !step_value) {
        report("typed for bounds cannot be materialized as its iterator type",
               statement.iterator->range);
        return next(block);
      }

      std::vector<Type> header_types{*iterator_type};
      header_types.insert(header_types.end(), carried_types.begin(),
                          carried_types.end());
      std::vector<Value> header_initial{*start};
      header_initial.insert(header_initial.end(), initial.begin(),
                            initial.end());
      std::vector<std::size_t> effect_carried;
      std::vector<Type> effect_types;
      for (std::size_t index = 0; index < carried_types.size(); ++index) {
        if (detail::is_effect_type(carried_types[index])) {
          effect_carried.push_back(index);
          effect_types.push_back(carried_types[index]);
        }
      }
      const Block header = edit_->block(header_types);
      const Block body = edit_->block(effect_types);
      const Block exit = edit_->block(carried_types);
      edit_->jump(block, header, std::move(header_initial));
      const std::vector<Value> header_arguments = header.arguments();

      const std::size_t outer_scope_depth = locals_.depth();
      const detail::Locals outer_locals = locals_;
      const std::size_t outer_residual_depth = residual_control_depth_;
      ++residual_control_depth_;
      locals_.push();
      const std::string state_name =
          "$for.iterator" + std::to_string(next_temporary_++);
      const std::string limit_name =
          "$for.limit" + std::to_string(next_temporary_++);
      const std::string step_name =
          "$for.step" + std::to_string(next_temporary_++);
      define(state_name, header_arguments.front(), statement.iterator->range);
      define(statement.iterator->name, header_arguments.front(),
             statement.iterator->range);
      define(limit_name, *bound, statement.expression.range);
      define(step_name, *step_value, statement.expression.range);
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        bind({carried_names[index], std::nullopt, statement.range, true},
             header_arguments[index + 1U]);
      }

      const auto infix = [](std::string symbol, std::string lhs,
                            std::string rhs) {
        return Module::Expression{
            Module::Expression::Kind::Infix, std::move(symbol),
            {Module::Expression::reference(std::move(lhs)),
             Module::Expression::reference(std::move(rhs))}};
      };
      auto [condition_tail, condition] = instantiate_expression(
          infix(step > 0 ? "<" : ">", state_name, limit_name),
          statement.expression.range, header);
      const auto i1 = compiler_.make("i1");
      if (!condition || condition->known() || !i1 ||
          condition->type() != *i1) {
        report("typed for comparison must produce i1",
               statement.expression.range);
        return next(block);
      }
      std::vector<Value> exit_values(header_arguments.begin() + 1,
                                     header_arguments.end());
      std::vector<Value> body_values;
      body_values.reserve(effect_carried.size());
      for (const std::size_t index : effect_carried) {
        body_values.push_back(header_arguments[index + 1U]);
      }
      edit_->branch(condition_tail, *condition, body, body_values, exit,
                    std::move(exit_values));

      for (std::size_t index = 0; index < effect_carried.size(); ++index) {
        define(carried_names[effect_carried[index]], body.arguments()[index],
               statement.range);
      }

      const detail::Locals loop_locals = locals_;
      loops_.push_back({});
      Flow body_flow = instantiate_sequence(statement.body, body);
      loops_.pop_back();

      const auto carried_values = [&](const Path& active) {
        std::vector<Value> values;
        restore(active);
        for (std::size_t index = 0; index < carried_names.size(); ++index) {
          if (auto value = lookup(carried_names[index])) {
            auto carried = materialize(*value, carried_types[index],
                                       active.block, statement.range);
            if (carried) {
              values.push_back(*carried);
            }
          }
        }
        if (values.size() != carried_names.size()) {
          report("for body does not produce every loop-carried value",
                 statement.range);
        }
        return values;
      };

      for (const Path& active : body_flow.breaks) {
        auto values = carried_values(active);
        if (values.size() == carried_names.size()) {
          edit_->jump(active.block, exit, std::move(values));
        }
      }
      body_flow.next.insert(
          body_flow.next.end(),
          std::make_move_iterator(body_flow.continues.begin()),
          std::make_move_iterator(body_flow.continues.end()));
      if (!body_flow.next.empty()) {
        const Block latch = edit_->block(carried_types);
        const std::vector<Value> latch_arguments = latch.arguments();
        for (const Path& active : body_flow.next) {
          auto values = carried_values(active);
          if (values.size() == carried_names.size()) {
            edit_->jump(active.block, latch, std::move(values));
          }
        }
        locals_ = loop_locals;
        for (std::size_t index = 0; index < carried_names.size(); ++index) {
          bind({carried_names[index], std::nullopt, statement.range, true},
               latch_arguments[index]);
        }
        auto [increment_tail, increment] = instantiate_expression(
            infix("+", state_name, step_name), statement.iterator->range,
            latch);
        if (!increment || increment->known() ||
            increment->type() != *iterator_type) {
          report("typed for increment has the wrong iterator type",
                 statement.iterator->range);
          return next(block);
        }
        std::vector<Value> next_values{*increment};
        next_values.insert(next_values.end(), latch_arguments.begin(),
                           latch_arguments.end());
        edit_->jump(increment_tail, header, std::move(next_values));
      }

      locals_ = outer_locals;
      locals_.resize(outer_scope_depth);
      residual_control_depth_ = outer_residual_depth;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        bind({carried_names[index], std::nullopt, statement.range, true},
             exit.arguments()[index]);
      }
      return next(exit);
    }

    std::vector<Path> active{path(block)};
    Flow exits;
    for (detail::ExecutionValue& element : *elements) {
      if (loop_iterations_++ >= compiler_.evaluation_limits().steps) {
        report("compile-time for iteration limit exceeded", statement.range);
        break;
      }
      std::vector<Path> following;
      for (const Path& path : active) {
        restore(path);
        auto iterator = detail::stage(compiler_, element);
        if (!iterator) {
          report("for iterator cannot enter staged evaluation",
                 statement.iterator->range);
          continue;
        }
        const std::size_t outer_depth = locals_.depth();
        locals_.push();
        define_staged(statement.iterator->name, std::move(*iterator),
                      statement.iterator->range);
        loops_.push_back({});
        Flow flow = instantiate_sequence(statement.body, path.block);
        loops_.pop_back();
        trim_scopes(flow, outer_depth);
        exits.next.insert(exits.next.end(),
                          std::make_move_iterator(flow.breaks.begin()),
                          std::make_move_iterator(flow.breaks.end()));
        following.insert(following.end(),
                         std::make_move_iterator(flow.next.begin()),
                         std::make_move_iterator(flow.next.end()));
        following.insert(following.end(),
                         std::make_move_iterator(flow.continues.begin()),
                         std::make_move_iterator(flow.continues.end()));
      }
      active = std::move(following);
      if (active.empty()) {
        break;
      }
    }
    exits.next.insert(exits.next.end(), std::make_move_iterator(active.begin()),
                      std::make_move_iterator(active.end()));
    return exits;
  }

  Flow instantiate_return(std::span<const detail::ExpressionSyntax> expressions,
                          detail::SyntaxRange range, Block block) {
    std::vector<Value> returned;
    if (expressions.size() != result_types_.size()) {
      report("function return count does not match its result signature",
             range);
    } else {
      for (std::size_t index = 0; index < expressions.size(); ++index) {
        std::optional<Value> value;
        const auto& expression = expressions[index];
        const bool reference =
            (expression.value.kind == Module::Expression::Kind::Reference ||
             expression.value.kind == Module::Expression::Kind::Variable) &&
            expression.value.arguments.empty();
        if (reference) {
          value = use(expression);
        } else {
          const std::string name =
              "$return" + std::to_string(next_temporary_++);
          expected_values_.insert_or_assign(name, result_types_[index]);
          detail::StatementSyntax statement;
          statement.bindings.push_back({name, std::nullopt, expression.range});
          statement.expression = expression;
          statement.range = expression.range;
          Flow flow = instantiate_statement(statement, block);
          if (flow.next.size() == 1U && flow.breaks.empty() &&
              flow.continues.empty()) {
            restore(flow.next.front());
            block = flow.next.front().block;
            value = use(detail::LocalUseSyntax{name, expression.range});
          }
        }
        if (!value) {
          continue;
        }
        if (value->known() && value->type() != result_types_[index]) {
          value = materialize(*value, result_types_[index], block,
                              expression.range);
        }
        if (!value || value->type() != result_types_[index]) {
          report("returned value type does not match function result " +
                     std::to_string(index),
                 expression.range);
          continue;
        }
        returned.push_back(*value);
      }
    }
    edit_->ret(block, std::move(returned));
    return {};
  }

  Flow instantiate_loop_control(detail::Control kind, detail::SyntaxRange range,
                                Block block) {
    if (loops_.empty()) {
      report("loop control is outside a structured loop", range);
      return {};
    }
    const LoopContext& loop = loops_.back();
    const std::optional<Block>& target = kind == detail::Control::Continue
                                             ? loop.continue_target
                                             : loop.break_target;
    if (!target) {
      return transfer(kind == detail::Control::Break, block);
    }

    std::vector<Value> carried;
    for (std::size_t index = 0; index < loop.carried_names.size(); ++index) {
      auto value = lookup(loop.carried_names[index]);
      if (!value) {
        report("loop-carried value '" + loop.carried_names[index] +
                   "' is unavailable at control transfer",
               range);
        continue;
      }
      auto materialized =
          materialize(*value, loop.carried_types[index], block, range);
      if (materialized) {
        carried.push_back(*materialized);
      }
    }
    if (carried.size() != loop.carried_names.size()) {
      return {};
    }
    edit_->jump(block, *target, std::move(carried));
    return {};
  }

  Flow instantiate_statement(const detail::StatementSyntax& statement,
                             Block block) {
    if (statement.kind == detail::StatementSyntax::Kind::Return) {
      return instantiate_return(statement.values, statement.range, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Break) {
      return instantiate_loop_control(detail::Control::Break, statement.range,
                                      block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Continue) {
      return instantiate_loop_control(detail::Control::Continue,
                                      statement.range, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::If) {
      return instantiate_if_statement(statement, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::While) {
      return instantiate_while(statement, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::For) {
      return instantiate_for(statement, block);
    }
    using Kind = Module::Expression::Kind;
    const Module::Expression& expression = statement.expression.value;
    if (expression.kind == Kind::Variable ||
        expression.kind == Kind::Reference) {
      if (statement.bindings.size() != 1U || !expression.arguments.empty()) {
        report("a value reference must bind exactly one value",
               statement.range);
        return next(block);
      }
      auto expected = expected_type(statement.bindings.front());
      if (auto value = use(statement.expression, expected)) {
        if (expected && value->type() != *expected) {
          if (value->known()) {
            value = materialize(*value, *expected, block, statement.range);
          } else {
            report("referenced value does not match its annotated type",
                   statement.range);
            value.reset();
          }
        }
        if (value) {
          bind(statement.bindings.front(), std::move(*value));
        }
      }
      return next(block);
    }
    if (expression.kind != Kind::If) {
      instantiate_call(statement, block);
      return next(block);
    }
    if (expression.arguments.size() != 3U || statement.bindings.size() != 1U) {
      report("if expression must have one condition, two values, and one "
             "result",
             statement.range);
      return next(block);
    }

    const detail::ExpressionSyntax condition_syntax{expression.arguments[0],
                                                    statement.expression.range};
    if (known_result(condition_syntax.value, condition_syntax.range)) {
      auto condition = evaluate_known(condition_syntax);
      const auto selected =
          condition ? detail::known_boolean(*condition) : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               condition_syntax.range);
        return next(block);
      }
      detail::StatementSyntax selected_statement = statement;
      selected_statement.expression.value =
          expression.arguments[*selected ? 1U : 2U];
      return instantiate_statement(selected_statement, block);
    }

    auto condition = use(condition_syntax);
    if (!condition) {
      return next(block);
    }
    const Block yes = edit_->block();
    const Block no = edit_->block();
    edit_->branch(block, *condition, yes, {}, no, {});
    ++residual_control_depth_;
    auto [true_tail, true_value] = instantiate_expression(
        expression.arguments[1], statement.expression.range, yes);
    auto [false_tail, false_value] = instantiate_expression(
        expression.arguments[2], statement.expression.range, no);
    --residual_control_depth_;
    if (!true_value || !false_value) {
      return next(block);
    }
    auto merge_type = expected_type(statement.bindings.front());
    if (true_value->known() && false_value->known() &&
        *true_value == *false_value && !merge_type) {
      const Block merge = edit_->block();
      edit_->jump(true_tail, merge);
      edit_->jump(false_tail, merge);
      bind(statement.bindings.front(), std::move(*true_value));
      return next(merge);
    }
    if (!merge_type) {
      if (!true_value->known()) {
        merge_type = true_value->type();
      } else if (!false_value->known()) {
        merge_type = false_value->type();
      } else if (true_value->type() == false_value->type()) {
        merge_type = true_value->type();
      }
    }
    if (!merge_type) {
      report("Known branch values need an expected module type for "
             "materialization",
             statement.range);
      return next(block);
    }
    if ((!true_value->known() && true_value->type() != *merge_type) ||
        (!false_value->known() && false_value->type() != *merge_type)) {
      report("if branches produce different types", statement.range);
      return next(block);
    }
    auto materialized_true =
        materialize(*true_value, *merge_type, true_tail, statement.range);
    auto materialized_false =
        materialize(*false_value, *merge_type, false_tail, statement.range);
    if (!materialized_true || !materialized_false) {
      return next(block);
    }
    const Block merge = edit_->block({*merge_type});
    edit_->jump(true_tail, merge, {*materialized_true});
    edit_->jump(false_tail, merge, {*materialized_false});
    bind(statement.bindings.front(), merge.arguments().front());
    return next(merge);
  }

  void instantiate_evaluate_call(const detail::StatementSyntax& statement,
                                 const Module::Expression& expression,
                                 Block block) {
    const auto reject_results = [&] { invalidate(statement.bindings); };
    const SourceRange call_site = source(statement.expression.range);
    const detail::ExecuteFunction execute =
        [&](Module::FunctionDecl function,
            std::vector<detail::ExecutionValue> arguments, SourceRange) {
          return detail::CompilerAccess::execute(
              compiler_, std::move(function), std::move(arguments),
              residual_control_depth_ != 0U);
        };
    const detail::EvaluateCallArgument evaluate =
        [&](const Module::Expression& argument,
            const Module::ParameterDecl* expected)
            -> std::optional<detail::ExecutionValue> {
      if (argument.kind == Module::Expression::Kind::Lambda) {
        return compiler_function(argument, statement.expression.range);
      }
      if ((argument.kind == Module::Expression::Kind::Reference ||
           argument.kind == Module::Expression::Kind::Variable) &&
          argument.arguments.empty()) {
        const auto local = lookup_staged(argument.text);
        const auto* known = local ? local->known_value() : nullptr;
        if (known) {
          return *known;
        }
      }
      auto staged = evaluate_known(
          {argument, statement.expression.range},
          expected ? std::optional<Module::ParameterDecl>{*expected}
                   : std::nullopt);
      const auto* known = staged ? staged->known_value() : nullptr;
      return known ? std::optional<detail::ExecutionValue>{*known}
                   : std::nullopt;
    };
    auto results = detail::execute_call(
        compiler_, owner_, expression, call_site, statement.bindings.size(),
        {}, diagnostics_, evaluate, execute);
    if (!results) {
      reject_results();
      return;
    }
    for (std::size_t index = 0; index < results->size(); ++index) {
      auto value = detail::stage(compiler_, std::move((*results)[index]));
      const auto expected = expected_type(statement.bindings[index]);
      if (!value) {
        report("explicit compiler call result does not match binding '" +
                   statement.bindings[index].name + "'",
               statement.bindings[index].range);
        reject_results();
        return;
      }
      if (expected && value->type() != *expected) {
        const auto known = detail::ir_value(compiler_, *value);
        auto converted =
            known ? materialize(*known, *expected, block,
                                statement.bindings[index].range)
                  : std::optional<Value>{};
        if (!converted) {
          reject_results();
          return;
        }
        bind(statement.bindings[index], std::move(*converted));
        continue;
      }
      bind_staged(statement.bindings[index], std::move(*value));
    }
  }

  void instantiate_call(const detail::StatementSyntax& statement, Block block) {
    using Kind = Module::Expression::Kind;
    const Kind expression_kind = statement.expression.value.kind;
    const bool require_known = expression_kind == Kind::Evaluate;
    const bool implicit_value =
        expression_kind == Kind::Number || expression_kind == Kind::Boolean ||
        expression_kind == Kind::String || expression_kind == Kind::List ||
        expression_kind == Kind::Reference ||
        expression_kind == Kind::Variable ||
        expression_kind == Kind::FunctionType;
    auto contextual_type = statement.bindings.size() == 1U
                               ? expected_type(statement.bindings.front())
                               : std::optional<Type>{};
    if (require_known && statement.expression.value.arguments.size() == 1U &&
        statement.expression.value.arguments.front().kind == Kind::Call) {
      instantiate_evaluate_call(
          statement, statement.expression.value.arguments.front(), block);
      return;
    }
    if (expression_kind == Kind::Lambda) {
      auto value = contextual_type
                       ? inline_function(statement.expression.value,
                                         *contextual_type,
                                         statement.expression.range)
                       : std::optional<Value>{};
      if (!value || statement.bindings.size() != 1U) {
        if (!contextual_type) {
          report("inline function needs a callable type context",
                 statement.expression.range);
        }
        invalidate(statement.bindings);
        return;
      }
      bind(statement.bindings.front(), std::move(*value));
      return;
    }
    auto expected_domain = contextual_type
                               ? detail::type_domain(*contextual_type)
                               : std::optional<Module::Expression>{};
    const bool can_materialize_known =
        implicit_value && statement.bindings.size() == 1U &&
        (expected_domain ||
         known_result(statement.expression.value, statement.expression.range)
             .has_value());
    if (require_known || can_materialize_known) {
      if (statement.bindings.size() != 1U) {
        report("compile-time evaluation must bind exactly one value",
               statement.range);
        return;
      }
      auto value = evaluate_known(
          statement.expression,
          expected_domain
              ? std::optional<Module::ParameterDecl>{{"result",
                                                      std::move(
                                                          *expected_domain),
                                                      false, std::nullopt}}
              : std::nullopt);
      if (value) {
        if (contextual_type && value->type() != *contextual_type) {
          auto ir = detail::ir_value(compiler_, *value);
          auto materialized =
              ir ? materialize(*ir, *contextual_type, block, statement.range)
                 : std::optional<Value>{};
          if (materialized) {
            bind(statement.bindings.front(), std::move(*materialized));
          } else {
            invalidate(statement.bindings);
          }
        } else {
          bind_staged(statement.bindings.front(), std::move(value));
        }
      } else {
        invalidate(statement.bindings);
      }
      return;
    }
    const auto invalidate_results = [&] { invalidate(statement.bindings); };
    const Module::Expression& expression = statement.expression.value;
    std::optional<Module::FunctionDecl::Fixity> fixity;
    if (expression.kind == Kind::Prefix) {
      fixity = Module::FunctionDecl::Fixity::Prefix;
    } else if (expression.kind == Kind::Infix) {
      fixity = Module::FunctionDecl::Fixity::Infix;
    } else if (expression.kind == Kind::Postfix) {
      fixity = Module::FunctionDecl::Fixity::Postfix;
    } else if (expression.kind != Kind::Call) {
      report("expression cannot be residualized as a call", statement.range);
      invalidate_results();
      return;
    }

    std::vector<PendingArgument> arguments;
    arguments.reserve(expression.arguments.size());
    for (const Module::Expression& argument_expression : expression.arguments) {
      detail::ExpressionSyntax argument_syntax{argument_expression,
                                               statement.expression.range};
      PendingArgument argument;
      if (argument_expression.kind == Kind::Lambda) {
        argument.inline_function = argument_expression;
        if (!argument_expression.arguments.empty() &&
            argument_expression.labels.size() + 1U ==
                argument_expression.arguments.size()) {
          std::vector<Type> inputs;
          inputs.reserve(argument_expression.labels.size());
          bool valid = true;
          for (std::size_t index = 0;
               index < argument_expression.labels.size(); ++index) {
            auto input = type({argument_expression.arguments[index],
                               statement.expression.range});
            valid = input.has_value() && valid;
            if (input) {
              inputs.push_back(std::move(*input));
            }
          }
          if (valid) {
            argument.inline_inputs = std::move(inputs);
          }
        }
      } else if ((argument_expression.kind == Kind::Reference ||
           argument_expression.kind == Kind::Variable) &&
          argument_expression.arguments.empty()) {
        argument.value = lookup_staged(argument_expression.text);
        if (!argument.value && declared_local(argument_expression.text)) {
          invalidate_results();
          return;
        }
        if (!argument.value && argument_expression.kind == Kind::Reference &&
            !visible_functions(argument_expression.text).empty()) {
          argument.function = argument_expression.text;
        } else if (!argument.value) {
          auto used = use(argument_syntax);
          argument.value = used ? detail::stage(std::move(*used))
                                : std::optional<detail::StagedValue>{};
        }
      } else if (known_result(argument_expression,
                              statement.expression.range)) {
        argument.value = evaluate_known(argument_syntax);
      } else {
        auto [argument_tail, value] = instantiate_expression(
            argument_expression, statement.expression.range, block);
        block = argument_tail;
        argument.value = value ? detail::stage(std::move(*value))
                               : std::optional<detail::StagedValue>{};
      }
      if (!argument.valid()) {
        invalidate_results();
        return;
      }
      arguments.push_back(std::move(argument));
    }

    std::vector<std::optional<Type>> expected_types;
    bool invalid_expected_type = false;
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      const auto& binding = statement.bindings[index];
      if (!binding.type) {
        const auto expected = locals_.depth() == 1U
                                  ? expected_values_.find(binding.name)
                                  : expected_values_.end();
        expected_types.push_back(expected == expected_values_.end()
                                     ? std::optional<Type>{}
                                     : std::optional<Type>{expected->second});
        continue;
      }
      auto resolved = type(*binding.type);
      invalid_expected_type = !resolved || invalid_expected_type;
      expected_types.push_back(std::move(resolved));
    }
    if (invalid_expected_type) {
      report("call result names and types have different counts",
             statement.range);
      invalidate_results();
      return;
    }

    std::vector<Module::FunctionDecl> declarations =
        fixity ? detail::visible_operators(compiler_, owner_, expression.text,
                                           *fixity)
               : visible_functions(expression.text);
    if (!declarations.empty() &&
        std::all_of(declarations.begin(), declarations.end(),
                    [](const Module::FunctionDecl& declaration) {
                      return !detail::compiler_results(declaration).empty();
                    })) {
      report("compiler-domain call '" + expression.text +
                 "' requires explicit @ evaluation",
             statement.expression.range);
      invalidate_results();
      return;
    }
    std::vector<PendingCall> plans;
    const bool unique_declaration = declarations.size() == 1U;
    const std::size_t diagnostics_before_planning = diagnostics_.size();
    for (const auto& function : declarations) {
      auto plan = plan_call(function, expression, arguments, expected_types,
                            statement.expression.range,
                            unique_declaration && residual_control_depth_ == 0U,
                            unique_declaration ? &diagnostics_ : nullptr);
      if (plan) {
        plans.push_back(std::move(*plan));
      }
    }
    if (plans.empty()) {
      if (diagnostics_.size() == diagnostics_before_planning) {
        report("no overload of '" + expression.text +
                   "' accepts the call arguments and expected results",
               statement.expression.range);
      }
      invalidate_results();
      return;
    }
    if (plans.size() != 1U) {
      std::string message =
          fixity ? "operator '" + expression.text + "' is ambiguous between"
                 : "call to '" + expression.text + "' is ambiguous between";
      for (const PendingCall& plan : plans) {
        message += " '" + plan.function.symbol().qualified_name() + "'";
      }
      report(std::move(message), statement.expression.range);
      invalidate_results();
      return;
    }

    PendingCall plan = std::move(plans.front());
    if (!detail::compiler_results(plan.function).empty()) {
      report("compiler-domain call '" +
                 plan.function.symbol().qualified_name() +
                 "' requires explicit @ evaluation",
             statement.expression.range);
      invalidate_results();
      return;
    }
    const auto parameters = plan.function.inputs();
    std::size_t argument_index = 0;
    bool unresolved = false;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (!detail::is_value_port(parameters[index])) {
        continue;
      }
      for (PendingArgument& argument : plan.arguments[index]) {
        if (argument.is_inline_function()) {
          auto value = inline_function(
              *argument.inline_function,
              plan.partial_types.arguments[argument_index],
              statement.expression.range);
          argument.value = value ? detail::stage(std::move(*value))
                                 : std::optional<detail::StagedValue>{};
          unresolved = !argument.value || unresolved;
        } else if (argument.is_function()) {
          auto reference =
              function_reference(argument.function, statement.expression.range,
                                 plan.partial_types.arguments[argument_index]);
          argument.value = reference ? detail::stage(std::move(*reference))
                                     : std::optional<detail::StagedValue>{};
          unresolved = !argument.value || unresolved;
        }
        ++argument_index;
      }
    }
    if (unresolved) {
      invalidate_results();
      return;
    }

    std::vector<Value> call_arguments;
    for (const auto& parameter_arguments : plan.arguments) {
      for (const PendingArgument& argument : parameter_arguments) {
        if (!argument.value) {
          invalidate_results();
          return;
        }
        auto value = detail::ir_value(compiler_, *argument.value);
        if (!value) {
          report("call argument cannot enter Residual IR",
                 statement.expression.range);
          invalidate_results();
          return;
        }
        call_arguments.push_back(std::move(*value));
      }
    }
    Op op =
        edit_->append(std::move(block), plan.function,
                      std::move(call_arguments), plan.partial_types.results);
    detail::FunctionAccess::locate(*edit_, op,
                                   source(statement.expression.range));
    if (statement.bindings.size() != op.results().size()) {
      report("call result count does not match its bindings", statement.range);
      invalidate_results();
      return;
    }
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      bind(statement.bindings[index], op.result(index));
    }
  }

  Compiler& compiler_;
  std::optional<Module::FunctionDecl> declaration_;
  const detail::FunctionBody& body_;
  std::string owner_;
  Diagnostics& diagnostics_;
  std::size_t initial_diagnostics_ = 0;
  std::optional<Function> function_;
  std::optional<Function::Edit> edit_;
  detail::Locals locals_;
  std::unordered_map<std::string, Type> expected_values_;
  std::vector<Type> result_types_;
  std::unordered_map<std::string, Block> blocks_;
  std::vector<LoopContext> loops_;
  std::size_t next_temporary_ = 0;
  std::size_t residual_control_depth_ = 0;
  std::size_t loop_iterations_ = 0;
  std::vector<Value> supplied_known_;
  detail::KnownBindings supplied_bindings_;
  std::vector<std::pair<std::string, Type>> inline_arguments_;
  std::optional<std::vector<Type>> inline_results_;
};

class RuntimeSyntax {
public:
  RuntimeSyntax(const Function& function, std::string_view name,
                bool qualify_types)
      : function_(function), qualify_types_(qualify_types) {
    syntax_.name = std::string(name);
  }

  detail::FunctionSyntax build() {
    for (const Value& argument : function_.arguments()) {
      const std::string name = bind(argument, "arg");
      syntax_.arguments.push_back({name, value(argument.type())});
    }
    for (const Type& result : function_.result_types()) {
      syntax_.result_types.push_back(value(result));
    }
    const auto blocks = function_.blocks();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
      block_names_.emplace_back(blocks[index],
                                index == 0U ? "entry"
                                            : "block" + std::to_string(index));
    }
    for (const Block& block : blocks) {
      for (const Value& argument : block.arguments()) {
        bind(argument, "arg");
      }
    }
    for (const Block& block : blocks) {
      for (const Op& op : block.ops()) {
        for (const Value& argument : op.arguments()) {
          remember_function(argument);
        }
      }
      const Terminator terminator = block.terminator();
      if (const auto condition = terminator.condition()) {
        remember_function(*condition);
      }
      for (const Value& returned : terminator.returned()) {
        remember_function(returned);
      }
      for (std::size_t index = 0; index < terminator.successor_count();
           ++index) {
        for (const Value& argument : terminator.arguments(index)) {
          remember_function(argument);
        }
      }
    }
    for (const Block& block : blocks) {
      detail::BlockSyntax syntax;
      syntax.name = block_name(block);
      for (const Value& argument : block.arguments()) {
        syntax.arguments.push_back(
            {use(argument), type_expression(argument.type()), {}});
      }
      if (block.is_entry()) {
        for (const Value& function : functions_) {
          syntax.statements.push_back(function_binding(function));
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
  static std::size_t
  visible_parameters(std::span<const ParameterValue> parameters,
                     std::span<const Module::ParameterDecl> schema) {
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

  detail::ValueSyntax value(const Type& type) const {
    detail::ValueSyntax result;
    result.kind = detail::ValueSyntax::Kind::Reference;
    result.text = qualify_types_
                      ? type.schema().symbol().qualified_name()
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

  detail::ValueSyntax value(const ParameterValue& parameter) const {
    detail::ValueSyntax result;
    switch (parameter.kind()) {
    case ParameterValue::Kind::I64:
      result.kind = detail::ValueSyntax::Kind::Number;
      result.text = std::to_string(*parameter.as_i64());
      break;
    case ParameterValue::Kind::F64: {
      result.kind = detail::ValueSyntax::Kind::Number;
      const auto text = detail::canonical_real(*parameter.as_f64());
      if (!text) {
        throw std::invalid_argument(
            "the Function contains an unformattable real parameter");
      }
      result.text = *text;
      break;
    }
    case ParameterValue::Kind::Boolean:
      result.kind = detail::ValueSyntax::Kind::Boolean;
      result.text = *parameter.as_bool() ? "true" : "false";
      break;
    case ParameterValue::Kind::String:
      result.kind = detail::ValueSyntax::Kind::String;
      result.text = *parameter.as_string();
      break;
    case ParameterValue::Kind::Type:
      return value(*parameter.as_type());
    case ParameterValue::Kind::List:
      result.kind = detail::ValueSyntax::Kind::List;
      for (const ParameterValue& element : parameter.elements()) {
        result.elements.push_back(value(element));
      }
      break;
    }
    return result;
  }

  std::string bind(const Value& value, std::string_view prefix) {
    const std::string name =
        std::string(prefix) + std::to_string(next_value_++);
    names_.emplace_back(value, name);
    return name;
  }

  void remember_function(const Value& value) {
    if ((!value.referenced_function() && !value.inline_function()) ||
        std::find(functions_.begin(), functions_.end(), value) !=
            functions_.end()) {
      return;
    }
    bind(value, "fn");
    functions_.push_back(value);
  }

  detail::StatementSyntax function_binding(const Value& value) const {
    const auto function = value.referenced_function();
    detail::StatementSyntax statement;
    statement.kind = detail::StatementSyntax::Kind::Expression;
    statement.bindings.push_back(
        {use(value), type_expression(value.type()), {}});
    if (function) {
      statement.expression.value.kind = Module::Expression::Kind::Reference;
      statement.expression.value.text = function->symbol().qualified_name();
      return statement;
    }
    const auto body = value.inline_function();
    const auto lambda = body ? inline_expression(*body)
                             : std::optional<Module::Expression>{};
    if (!lambda) {
      throw std::logic_error(
          "inline function cannot be represented as one expression");
    }
    statement.expression.value = *lambda;
    return statement;
  }

  std::optional<Module::Expression>
  inline_expression(const Function& function) const {
    using Kind = Module::Expression::Kind;
    const auto blocks = function.blocks();
    const auto returned = blocks.size() == 1U
                              ? blocks.front().terminator().returned()
                              : std::vector<Value>{};
    if (returned.size() != 1U || !blocks.front().arguments().empty()) {
      return std::nullopt;
    }
    const auto arguments = function.arguments();
    std::vector<std::string> names;
    names.reserve(arguments.size());
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      names.push_back("arg" + std::to_string(index));
    }
    std::function<std::optional<Module::Expression>(const Value&)> build =
        [&](const Value& current) -> std::optional<Module::Expression> {
      const auto argument =
          std::find(arguments.begin(), arguments.end(), current);
      if (argument != arguments.end()) {
        return Module::Expression{
            Kind::Variable,
            names[static_cast<std::size_t>(
                std::distance(arguments.begin(), argument))],
            {}};
      }
      if (const auto reference = current.referenced_function()) {
        return Module::Expression::reference(
            std::string(reference->symbol().qualified_name()));
      }
      if (const auto nested = current.inline_function()) {
        return inline_expression(*nested);
      }
      if (current.known()) {
        const auto payload = detail::FunctionAccess::known_value(current);
        return payload ? std::optional<Module::Expression>{
                             expression(value(*payload))}
                       : std::nullopt;
      }
      const auto producer = current.defining_op();
      if (!producer || producer->results().size() != 1U ||
          producer->result(0) != current || current.users().size() > 1U) {
        return std::nullopt;
      }
      Module::Expression result;
      result.kind = Kind::Call;
      result.text = producer->callee().symbol().qualified_name();
      for (const Value& operand : producer->arguments()) {
        auto built = build(operand);
        if (!built) {
          return std::nullopt;
        }
        result.arguments.push_back(std::move(*built));
        result.labels.emplace_back();
      }
      const auto fixity = producer->callee().operator_fixity();
      if (fixity && detail::compiler_inputs(producer->callee()).empty() &&
          ((fixity == Module::FunctionDecl::Fixity::Infix &&
            result.arguments.size() == 2U) ||
           (fixity != Module::FunctionDecl::Fixity::Infix &&
            result.arguments.size() == 1U))) {
        result.text = std::string(producer->callee().name());
        result.labels.clear();
        result.kind = fixity == Module::FunctionDecl::Fixity::Prefix
                          ? Kind::Prefix
                      : fixity == Module::FunctionDecl::Fixity::Infix
                          ? Kind::Infix
                          : Kind::Postfix;
      }
      return result;
    };
    auto body = build(returned.front());
    if (!body) {
      return std::nullopt;
    }
    Module::Expression lambda;
    lambda.kind = Kind::Lambda;
    lambda.labels = std::move(names);
    for (const Value& argument : arguments) {
      lambda.arguments.push_back(expression(value(argument.type())));
    }
    lambda.arguments.push_back(std::move(*body));
    return lambda;
  }

  std::string use(const Value& value) const {
    const auto found =
        std::find_if(names_.begin(), names_.end(),
                     [&](const auto& entry) { return entry.first == value; });
    if (found == names_.end()) {
      if (const auto function = value.referenced_function()) {
        return function->symbol().qualified_name();
      }
    }
    if (found == names_.end()) {
      throw std::invalid_argument(
          "the function contains a value before its definition");
    }
    return found->second;
  }

  std::string block_name(const Block& block) const {
    const auto found =
        std::find_if(block_names_.begin(), block_names_.end(),
                     [&](const auto& entry) { return entry.first == block; });
    if (found == block_names_.end()) {
      throw std::invalid_argument("the function has an unknown successor");
    }
    return found->second;
  }

  static Module::Expression expression(const detail::ValueSyntax& value) {
    using Kind = Module::Expression::Kind;
    Module::Expression result;
    switch (value.kind) {
    case detail::ValueSyntax::Kind::Number:
      result.kind = Kind::Number;
      break;
    case detail::ValueSyntax::Kind::Boolean:
      result.kind = Kind::Boolean;
      break;
    case detail::ValueSyntax::Kind::String:
      result.kind = Kind::String;
      break;
    case detail::ValueSyntax::Kind::List:
      result.kind = Kind::List;
      break;
    case detail::ValueSyntax::Kind::Reference:
      result.kind = Kind::Reference;
      break;
    }
    result.text = value.text;
    for (const auto& element : value.elements) {
      result.arguments.push_back(expression(element));
    }
    return result;
  }

  detail::ExpressionSyntax type_expression(const Type& type) const {
    return {expression(value(type)), {}};
  }

  detail::StatementSyntax convert(const Op& op) {
    detail::StatementSyntax result;
    result.expression.value.kind = Module::Expression::Kind::Call;
    result.expression.value.text =
        op.callee().symbol().qualified_name();
    for (const Value& output : op.results()) {
      result.bindings.push_back(
          {bind(output, "v"), type_expression(output.type()), {}});
    }
    const auto arguments = op.arguments();
    const auto parameters = op.callee().inputs();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const Value& argument = arguments[index];
      const std::size_t parameter_index =
          detail::FunctionAccess::argument_parameter(op, index);
      if (argument.known()) {
        const auto payload = detail::FunctionAccess::known_value(argument);
        if (!payload || parameter_index >= parameters.size()) {
          throw std::logic_error("op has an invalid Known argument");
        }
        result.expression.value.arguments.push_back(
            expression(value(*payload)));
        result.expression.value.labels.push_back(
            parameters[parameter_index].name);
      } else {
        result.expression.value.arguments.push_back(
            Module::Expression::reference(use(argument)));
        result.expression.value.labels.emplace_back();
      }
    }
    const auto fixity = op.callee().operator_fixity();
    const bool valid_arity =
        fixity && ((*fixity == Module::FunctionDecl::Fixity::Infix &&
                    op.arguments().size() == 2U) ||
                   (*fixity != Module::FunctionDecl::Fixity::Infix &&
                    op.arguments().size() == 1U));
    if (fixity && valid_arity && result.bindings.size() == 1U &&
        detail::compiler_inputs(op.callee()).empty()) {
      result.expression.value.text = std::string(op.callee().name());
      if (*fixity == Module::FunctionDecl::Fixity::Prefix) {
        result.expression.value.kind = Module::Expression::Kind::Prefix;
      } else if (*fixity == Module::FunctionDecl::Fixity::Infix) {
        result.expression.value.kind = Module::Expression::Kind::Infix;
      } else {
        result.expression.value.kind = Module::Expression::Kind::Postfix;
      }
      result.expression.value.labels.clear();
    }
    return result;
  }

  detail::TerminatorSyntax convert(const Terminator& terminator) const {
    detail::TerminatorSyntax result;
    if (terminator.kind() == Terminator::Kind::Return) {
      result.kind = detail::TerminatorSyntax::Kind::Return;
      for (const Value& value : terminator.returned()) {
        result.values.push_back(
            {Module::Expression::reference(use(value)), {}});
      }
      return result;
    }
    result.kind = terminator.kind() == Terminator::Kind::Jump
                      ? detail::TerminatorSyntax::Kind::Jump
                      : detail::TerminatorSyntax::Kind::Branch;
    if (const auto condition = terminator.condition()) {
      result.condition = detail::ExpressionSyntax{
          Module::Expression::reference(use(*condition)), {}};
    }
    for (std::size_t index = 0; index < terminator.successor_count(); ++index) {
      detail::SuccessorSyntax successor;
      successor.target = block_name(terminator.successor(index));
      for (const Value& argument : terminator.arguments(index)) {
        successor.arguments.push_back(
            {Module::Expression::reference(use(argument)), {}});
      }
      result.successors.push_back(std::move(successor));
    }
    return result;
  }

  const Function& function_;
  detail::FunctionSyntax syntax_;
  std::vector<std::pair<Value, std::string>> names_;
  std::vector<Value> functions_;
  std::vector<std::pair<Block, std::string>> block_names_;
  std::size_t next_value_ = 0;
  bool qualify_types_ = false;
};

}  // namespace

namespace detail {

bool verify_function_body(const FunctionBody& body, Diagnostics& diagnostics) {
  const std::size_t initial_diagnostics = diagnostics.size();
  const auto locate = [&](SyntaxRange range) -> std::optional<SourceRange> {
    if (body.source.empty()) {
      return std::nullopt;
    }
    return SourceRange{body.source, range.begin, range.end};
  };
  const auto report = [&](std::string message, SyntaxRange range) {
    diagnostics.report(std::move(message), locate(range));
  };

  if (body.blocks.empty()) {
    report("a function definition must have a body", body.range);
    return false;
  }

  if (body.blocks.front().name != "entry") {
    report("the first block must be named 'entry'", body.blocks.front().range);
  }
  if (!body.blocks.front().arguments.empty()) {
    report("the entry block cannot declare block arguments",
           body.blocks.front().range);
  }

  std::unordered_map<std::string_view, const BlockSyntax*> blocks;
  std::unordered_set<std::string_view> definitions;
  const auto falls_through =
      [&](const auto& self,
          const std::vector<StatementSyntax>& statements) -> bool {
    for (const StatementSyntax& statement : statements) {
      if (statement.kind == StatementSyntax::Kind::Return ||
          statement.kind == StatementSyntax::Kind::Break ||
          statement.kind == StatementSyntax::Kind::Continue) {
        return false;
      }
      if (statement.kind == StatementSyntax::Kind::If && statement.has_else &&
          !self(self, statement.body) && !self(self, statement.otherwise)) {
        return false;
      }
    }
    return true;
  };
  const auto check_statements =
      [&](const auto& self, const std::vector<StatementSyntax>& statements,
          std::unordered_set<std::string_view>& names) -> void {
    bool reachable = true;
    for (const StatementSyntax& statement : statements) {
      if (!reachable) {
        report("unreachable statement after a control transfer",
               statement.range);
      }
      if (statement.kind != StatementSyntax::Kind::Expression) {
        auto nested = names;
        if (statement.kind == StatementSyntax::Kind::For) {
          if (!statement.iterator) {
            report("for statement has no iterator", statement.range);
          } else {
            nested.insert(statement.iterator->name);
          }
        }
        self(self, statement.body, nested);
        if (statement.kind == StatementSyntax::Kind::If) {
          nested = names;
          self(self, statement.otherwise, nested);
        }
        if (statement.kind == StatementSyntax::Kind::Return ||
            statement.kind == StatementSyntax::Kind::Break ||
            statement.kind == StatementSyntax::Kind::Continue ||
            (statement.kind == StatementSyntax::Kind::If &&
             statement.has_else &&
             !falls_through(falls_through, statement.body) &&
             !falls_through(falls_through, statement.otherwise))) {
          reachable = false;
        }
        continue;
      }
      for (const BindingSyntax& binding : statement.bindings) {
        if (!binding.rebind && !names.insert(binding.name).second) {
          report("duplicate local value '" + binding.name + "'",
                 statement.range);
        }
      }
    }
  };
  for (const BlockSyntax& block : body.blocks) {
    if (block.name.empty()) {
      report("a block must have a name", block.range);
    } else if (!blocks.emplace(block.name, &block).second) {
      report("duplicate block '" + block.name + "'", block.range);
    }
    for (const BlockArgumentSyntax& argument : block.arguments) {
      if (!definitions.insert(argument.name).second) {
        report("duplicate local value '" + argument.name + "'", argument.range);
      }
    }
    check_statements(check_statements, block.statements, definitions);
  }

  const bool structured =
      body.blocks.size() == 1U && !body.blocks.front().terminator;
  if (structured &&
      falls_through(falls_through, body.blocks.front().statements)) {
    report("a function body has a path that does not return",
           body.blocks.front().range);
  }

  const auto check_successor = [&](const SuccessorSyntax& successor) {
    const auto target = blocks.find(successor.target);
    if (target == blocks.end()) {
      report("unknown successor block '" + successor.target + "'",
             successor.range);
      return;
    }
    if (target->second == &body.blocks.front()) {
      report("control flow cannot branch to the entry block", successor.range);
    }
    if (successor.arguments.size() != target->second->arguments.size()) {
      report("successor '" + successor.target + "' expects " +
                 std::to_string(target->second->arguments.size()) +
                 " block argument(s), but the edge provides " +
                 std::to_string(successor.arguments.size()),
             successor.range);
    }
  };

  for (const BlockSyntax& block : body.blocks) {
    if (!block.terminator) {
      if (!structured) {
        report("explicit block '" + block.name + "' has no terminator",
               block.range);
      }
      continue;
    }
    const TerminatorSyntax& terminator = *block.terminator;
    switch (terminator.kind) {
    case TerminatorSyntax::Kind::Return:
      if (terminator.condition || !terminator.successors.empty()) {
        report("return cannot have a condition or successor", terminator.range);
      }
      break;
    case TerminatorSyntax::Kind::Jump:
      if (terminator.condition || !terminator.values.empty() ||
          terminator.successors.size() != 1U) {
        report("jump must have exactly one successor", terminator.range);
      }
      break;
    case TerminatorSyntax::Kind::Branch:
      if (!terminator.condition || !terminator.values.empty() ||
          terminator.successors.size() != 2U) {
        report("branch must have one condition and two successors",
               terminator.range);
      }
      break;
    }
    for (const SuccessorSyntax& successor : terminator.successors) {
      check_successor(successor);
    }
  }

  if (blocks.size() == body.blocks.size() &&
      blocks.find("entry") != blocks.end()) {
    std::unordered_set<std::string_view> reachable{"entry"};
    std::vector<std::string_view> pending{"entry"};
    while (!pending.empty()) {
      const std::string_view name = pending.back();
      pending.pop_back();
      const BlockSyntax* block = blocks.at(name);
      if (!block->terminator) {
        continue;
      }
      for (const SuccessorSyntax& successor : block->terminator->successors) {
        if (blocks.find(successor.target) != blocks.end() &&
            reachable.insert(successor.target).second) {
          pending.push_back(successor.target);
        }
      }
    }
    for (const BlockSyntax& block : body.blocks) {
      if (reachable.find(block.name) == reachable.end()) {
        report("unreachable block '" + block.name + "'", block.range);
      }
    }
  }

  return diagnostics.size() == initial_diagnostics;
}

std::optional<FunctionBody> parse_function_body(
    Lexer& lexer, Token& current, Diagnostics& diagnostics, std::string source,
    std::span<const Module::FunctionDecl::GenericDecl> variables) {
  return SyntaxParser(lexer, current, diagnostics, std::move(source), variables)
      .parse();
}

std::string format_function_body(const FunctionBody& body, std::size_t indent) {
  return SyntaxWriter(body, indent).write();
}

std::string format_function_syntax(const FunctionSyntax& function,
                                   std::size_t indent) {
  return SyntaxWriter(function, indent).write();
}

FunctionSyntax materialized_function_syntax(const Function& function,
                                            std::string_view name,
                                            bool qualify_types) {
  return RuntimeSyntax(function, name, qualify_types).build();
}

std::optional<Function>
instantiate_function(Compiler& compiler, Module::FunctionDecl function,
                     const FunctionBody& body, Diagnostics& diagnostics,
                     std::vector<Value> known_arguments,
                     KnownBindings bindings) {
  return Instantiator(compiler, std::move(function), body, diagnostics,
                      std::move(known_arguments), std::move(bindings))
      .instantiate();
}

std::optional<Function> instantiate_lambda(
    Compiler& compiler, std::string_view owner,
    const Module::Expression& expression, const SourceRange& source,
    Diagnostics& diagnostics, const KnownBindings& bindings,
    std::optional<std::vector<Type>> expected_inputs,
    std::optional<std::vector<Type>> expected_results,
    bool allow_guarded_evaluation) {
  using Kind = Module::Expression::Kind;
  const auto report = [&](std::string message) {
    diagnostics.report(std::move(message), source);
  };
  if (expression.kind != Kind::Lambda || expression.arguments.empty() ||
      expression.labels.size() + 1U != expression.arguments.size()) {
    report("malformed inline function");
    return std::nullopt;
  }
  if (expected_inputs &&
      expected_inputs->size() != expression.labels.size()) {
    report("inline function does not match its callable context");
    return std::nullopt;
  }

  const Module::ParameterDecl expected_type{
      "lambda parameter type", domain_expression(ValueKind::Type), false,
      std::nullopt};
  std::vector<std::pair<std::string, Type>> arguments;
  arguments.reserve(expression.labels.size());
  for (std::size_t index = 0; index < expression.labels.size(); ++index) {
    auto value = evaluate_known_expression(
        compiler, owner, expression.arguments[index], expected_type, bindings,
        diagnostics, source, allow_guarded_evaluation);
    const Type* annotation = value ? value->as_type() : nullptr;
    if (annotation == nullptr ||
        (expected_inputs && *annotation != (*expected_inputs)[index])) {
      report("inline function parameter '" + expression.labels[index] +
             "' does not match its callable context");
      return std::nullopt;
    }
    arguments.emplace_back(expression.labels[index], *annotation);
  }

  FunctionBody body;
  body.source = source.source;
  body.range = {source.begin, source.end};
  BlockSyntax entry;
  entry.name = "entry";
  entry.range = body.range;
  StatementSyntax returned;
  returned.kind = StatementSyntax::Kind::Return;
  returned.range = body.range;
  returned.values.push_back({expression.arguments.back(), body.range});
  entry.statements.push_back(std::move(returned));
  body.blocks.push_back(std::move(entry));

  return Instantiator(compiler, std::string(owner), body, diagnostics,
                      std::move(arguments), std::move(expected_results))
      .instantiate();
}

}  // namespace detail

std::string format(const Function& function, std::string_view name) {
  const auto identifier_character = [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  };
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_') ||
      !std::all_of(name.begin() + 1, name.end(), identifier_character)) {
    throw std::invalid_argument("a formatted Function needs a valid name");
  }
  return detail::format_function_syntax(
      detail::materialized_function_syntax(function, name), 0U);
}

}  // namespace joggle
