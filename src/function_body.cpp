#include "function_body.h"

#include "expression_syntax.h"
#include "prelude.h"
#include "compiler_internal.h"

#include "diagnostic_internal.h"
#include "domain.h"
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

  std::optional<std::string> local_name() {
    return name("a local value name");
  }

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
        argument.type = parse_value();
        argument.range = {argument_begin, previous_end_};
        variables_.push_back(
            {argument.name, Module::Expression::reference("type"),
             std::nullopt});
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

  detail::ValueSyntax parse_value() {
    const SourcePosition begin = current_.begin;
    if (is(TokenKind::String)) {
      detail::ValueSyntax result{
          detail::ValueSyntax::Kind::String, current_.text, {}, {}};
      advance();
      result.range = {begin, previous_end_};
      return result;
    }
    const bool negative = match(TokenKind::Minus);
    if (is(TokenKind::Integer) || is(TokenKind::Number)) {
      detail::ValueSyntax result{
          detail::ValueSyntax::Kind::Number,
          negative ? "-" + current_.text : current_.text, {}, {}};
      advance();
      result.range = {begin, previous_end_};
      return result;
    }
    if (negative) {
      error("expected a number after '-'");
      return {detail::ValueSyntax::Kind::Number, {}, {},
              {begin, previous_end_}};
    }
    if (match_name("true")) {
      detail::ValueSyntax result{
          detail::ValueSyntax::Kind::Boolean, "true", {}, {}};
      result.range = {begin, previous_end_};
      return result;
    }
    if (match_name("false")) {
      detail::ValueSyntax result{
          detail::ValueSyntax::Kind::Boolean, "false", {}, {}};
      result.range = {begin, previous_end_};
      return result;
    }
    if (match(TokenKind::LeftBracket)) {
      detail::ValueSyntax result{detail::ValueSyntax::Kind::List, {}, {}, {}};
      if (!match(TokenKind::RightBracket)) {
        do {
          result.elements.push_back(parse_value());
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightBracket, "']'");
      }
      result.range = {begin, previous_end_};
      return result;
    }
    auto symbol = reference("a type or attribute name");
    detail::ValueSyntax result{detail::ValueSyntax::Kind::Reference,
                               symbol.value_or(std::string{}),
                               {},
                               {}};
    if (match(TokenKind::Less)) {
      if (!match(TokenKind::Greater)) {
        do {
          result.elements.push_back(parse_value());
        } while (match(TokenKind::Comma));
        expect(TokenKind::Greater, "'>'");
      }
    }
    result.range = {begin, previous_end_};
    return result;
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
    if (starts_binding()) {
      auto add_binding = [&](std::string name, SourcePosition binding_begin) {
        detail::BindingSyntax binding;
        binding.name = std::move(name);
        binding.rebind = locals_.contains(binding.name);
        if (match(TokenKind::Colon)) {
          binding.type = parse_value();
        }
        binding.range = {binding_begin, previous_end_};
        if (!binding.rebind) {
          locals_.insert(binding.name);
          variables_.push_back(
              {binding.name, Module::Expression::reference("type"),
               std::nullopt});
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
      error("operation-owned bodies were removed; pass a closure as an "
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
    const bool straight_line =
        body_->blocks.size() == 1U &&
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
          write_value(block.arguments[argument].type);
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

  void
  write_arguments(const std::vector<detail::FunctionArgumentSyntax>& arguments) {
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
        prefix_width += 2U + value_width(*statement.bindings[index].type);
        write_value(*statement.bindings[index].type);
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

std::optional<detail::ValueSyntax> value_syntax(
    const Module::Expression& expression, detail::SyntaxRange range) {
  detail::ValueSyntax result;
  result.range = range;
  using Kind = Module::Expression::Kind;
  switch (expression.kind) {
  case Kind::Number:
    result.kind = detail::ValueSyntax::Kind::Number;
    break;
  case Kind::Boolean:
    result.kind = detail::ValueSyntax::Kind::Boolean;
    break;
  case Kind::String:
    result.kind = detail::ValueSyntax::Kind::String;
    break;
  case Kind::List:
    result.kind = detail::ValueSyntax::Kind::List;
    break;
  case Kind::Reference:
  case Kind::Variable:
    result.kind = detail::ValueSyntax::Kind::Reference;
    break;
  case Kind::Call:
  case Kind::If:
  case Kind::Evaluate:
  case Kind::Prefix:
  case Kind::Infix:
  case Kind::Postfix:
    return std::nullopt;
  }
  result.text = expression.kind == Kind::Reference
                    ? detail::display_type_name(expression.text)
                    : expression.text;
  for (const auto& argument : expression.arguments) {
    auto converted = value_syntax(argument, range);
    if (!converted) {
      return std::nullopt;
    }
    result.elements.push_back(std::move(*converted));
  }
  return result;
}

class Instantiator {
  struct Flow {
    enum class Kind { Next, Break, Continue, Stop };

    Kind kind = Kind::Stop;
    std::optional<Block> block;

    static Flow next(Block value) { return {Kind::Next, std::move(value)}; }
    static Flow stop() { return {}; }
    static Flow transfer(Kind kind, Block value) {
      return {kind, std::move(value)};
    }
  };

  struct LoopContext {
    std::optional<Block> continue_target;
    std::optional<Block> break_target;
    std::vector<std::string> carried_names;
    std::vector<Type> carried_types;
  };

public:
  Instantiator(Compiler& compiler, Module::FunctionDecl function,
               const detail::FunctionBody& body, Diagnostics& diagnostics,
               std::vector<Value> known_arguments)
      : compiler_(compiler), declaration_(std::move(function)), body_(body),
        owner_(declaration_.symbol().module_name()),
        diagnostics_(diagnostics), initial_diagnostics_(diagnostics.size()),
        supplied_known_(std::move(known_arguments)) {}

  std::optional<Function> instantiate() {
    function_ = compiler_.function();
    if (!function_) {
      return std::nullopt;
    }
    const auto& contract = detail::FunctionTypeAccess::get(declaration_);
    const auto parameters = declaration_.inputs();
    std::vector<std::optional<Value>> known_parameters(parameters.size());
    detail::KnownBindings bindings;
    std::size_t supplied = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (contract.ir_inputs[index]) {
        continue;
      }
      const Module::ParameterDecl& parameter = parameters[index];
      std::size_t required_after = 0;
      for (std::size_t next = index + 1U; next < parameters.size(); ++next) {
        if (!contract.ir_inputs[next] && !parameters[next].default_value) {
          ++required_after;
        }
      }
      const bool use_default = parameter.default_value &&
                               supplied_known_.size() - supplied ==
                                   required_after;
      std::optional<Value> value;
      if (!use_default && supplied < supplied_known_.size()) {
        value = supplied_known_[supplied++];
      } else if (parameter.default_value) {
        auto payload = detail::parameter_default(parameter);
        auto type = reflected_type(parameter.domain);
        value = payload && type
                    ? compiler_.known(std::move(*type), std::move(*payload))
                    : std::optional<Value>{};
      }
      const auto payload = value && value->known()
                               ? detail::FunctionAccess::known_value(*value)
                               : std::nullopt;
      if (!value || !payload ||
          !detail::matches_parameter(parameter, *payload)) {
        report("function specialization needs a compatible Known argument '" +
                   parameter.name + "'",
               body_.range);
        continue;
      }
      known_parameters[index] = *value;
      bindings.insert_or_assign(parameter.name, *payload);
      if (index < contract.bindings.size() && contract.bindings[index] &&
          contract.bindings[index]->kind ==
              Module::Expression::Kind::Variable) {
        bindings.insert_or_assign(contract.bindings[index]->text, *payload);
      }
    }
    if (supplied != supplied_known_.size()) {
      report("function specialization has too many Known arguments",
             body_.range);
    }
    if (!ok()) {
      return std::nullopt;
    }

    const auto resolve_type = [&](const Module::Expression& expression,
                                  std::string_view role)
        -> std::optional<Type> {
      const Module::ParameterDecl expected{
          std::string(role), detail::domain_expression(detail::ValueKind::Type),
          false, std::nullopt};
      auto value = detail::evaluate_known_expression(
          compiler_, owner_, expression, expected, bindings, diagnostics_,
          source(body_.range));
      const Type* type = value ? value->as_type() : nullptr;
      if (type == nullptr) {
        report("cannot resolve " + std::string(role) + " type during "
               "function specialization",
               body_.range);
        return std::nullopt;
      }
      return *type;
    };

    result_types_.clear();
    const auto results = detail::ir_results(declaration_);
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
            if ((expression.value.kind !=
                     Module::Expression::Kind::Reference &&
                 expression.value.kind !=
                     Module::Expression::Kind::Variable) ||
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
      if (contract.ir_inputs[index]) {
        if (auto argument_type =
                resolve_type(parameters[index].domain, "input")) {
          argument_types.push_back(*argument_type);
        }
      }
    }
    if (argument_types.size() != detail::ir_inputs(declaration_).size() ||
        result_types_.size() != results.size()) {
      return std::nullopt;
    }

    detail::FunctionAccess::declare(*function_, declaration_, argument_types,
                                    result_types_);
    edit_.emplace(function_->edit());
    scopes_.emplace_back();
    std::size_t residual = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (contract.ir_inputs[index]) {
        define(parameters[index].name,
               edit_->argument(argument_types[residual++]), body_.range);
      } else if (known_parameters[index]) {
        define(parameters[index].name, *known_parameters[index], body_.range);
      }
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
           index < block.arguments.size() && index < arguments.size(); ++index) {
        define(block.arguments[index].name, arguments[index],
               block.arguments[index].range);
      }
    }

    for (const detail::BlockSyntax& block : body_.blocks) {
      const auto ir = blocks_.find(block.name);
      if (ir == blocks_.end()) {
        continue;
      }
      Flow current = Flow::next(ir->second);
      for (const auto& statement : block.statements) {
        if (current.kind != Flow::Kind::Next || !current.block) {
          break;
        }
        current = instantiate_statement(statement, *current.block);
      }
      if (current.kind == Flow::Kind::Break ||
          current.kind == Flow::Kind::Continue) {
        report("loop control escaped its structured loop", block.range);
        continue;
      }
      if (current.kind == Flow::Kind::Stop || !current.block) {
        continue;
      }
      if (!block.terminator) {
        report("function path falls through without returning", block.range);
        continue;
      }
      const detail::TerminatorSyntax& terminator = *block.terminator;
      if (terminator.kind == detail::TerminatorSyntax::Kind::Return) {
        instantiate_return(terminator.values, terminator.range,
                           *current.block);
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
      if (terminator.kind == detail::TerminatorSyntax::Kind::Jump) {
        if (auto destination = target(0)) {
          edit_->jump(*current.block, *destination,
                      edge_arguments.empty() ? std::vector<Value>{}
                                             : std::move(edge_arguments[0]));
        }
      } else {
        auto condition = terminator.condition
                             ? use(*terminator.condition)
                             : std::optional<Value>{};
        auto true_target = target(0);
        auto false_target = target(1);
        if (!condition || !true_target || !false_target ||
            edge_arguments.size() != 2U) {
          continue;
        }
        edit_->branch(*current.block, *condition, *true_target,
                      std::move(edge_arguments[0]), *false_target,
                      std::move(edge_arguments[1]));
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
    const bool prelude_type =
        std::is_same_v<Declaration, Module::TypeDecl> &&
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
    } else if constexpr (std::is_same_v<Declaration, Module::AttributeDecl>) {
      result = module->attribute(local);
    } else {
      result = module->function(local);
    }
    if (!result) {
      constexpr std::string_view kind =
          std::is_same_v<Declaration, Module::TypeDecl>
              ? "type"
              : (std::is_same_v<Declaration, Module::AttributeDecl>
                     ? "attribute"
                     : "function");
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
    const auto module = module_name ? compiler_.module(*module_name)
                                    : std::optional<Module>{};
    if (!module) {
      return std::nullopt;
    }
    if (module->type(local)) {
      return Module::SymbolKind::Type;
    }
    if (module->attribute(local)) {
      return Module::SymbolKind::Attribute;
    }
    return std::nullopt;
  }

  std::optional<ParameterValue>
  parameter(const detail::ValueSyntax& syntax,
            const Module::ParameterDecl& expected) {
    const auto domain = detail::kernel_domain(expected.domain);
    if (!domain) {
      report("unknown parameter domain for '" + expected.name + "'",
             syntax.range);
      return std::nullopt;
    }
    if (syntax.kind == detail::ValueSyntax::Kind::Reference &&
        syntax.elements.empty()) {
      auto local = lookup(syntax.text);
      auto expected_type = reflected_type(expected.domain);
      if (local && local->known() && expected_type &&
          local->type() == *expected_type) {
        if (auto value = detail::FunctionAccess::known_value(*local)) {
          return *value;
        }
      }
    }
    if (domain->list) {
      if (syntax.kind != detail::ValueSyntax::Kind::List) {
        report("expected a list value for parameter '" + expected.name + "'",
               syntax.range);
        return std::nullopt;
      }
      std::vector<ParameterValue> elements;
      for (const auto& element : syntax.elements) {
        Module::ParameterDecl scalar = expected;
        scalar.domain = detail::domain_expression(domain->element);
        auto value = parameter(element, scalar);
        if (!value) {
          return std::nullopt;
        }
        elements.push_back(std::move(*value));
      }
      return ParameterValue::list(std::move(elements));
    }
    switch (domain->element) {
    case detail::ValueKind::Integer: {
      if (syntax.kind != detail::ValueSyntax::Kind::Number) {
        break;
      }
      std::int64_t value = 0;
      const auto parsed = std::from_chars(
          syntax.text.data(), syntax.text.data() + syntax.text.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == syntax.text.data() + syntax.text.size()) {
        return ParameterValue(value);
      }
      break;
    }
    case detail::ValueKind::Real: {
      if (syntax.kind != detail::ValueSyntax::Kind::Number) {
        break;
      }
      double value = 0.0;
      std::istringstream input(syntax.text);
      input.imbue(std::locale::classic());
      input >> value;
      if (input && input.peek() == std::char_traits<char>::eof()) {
        return ParameterValue(value);
      }
      break;
    }
    case detail::ValueKind::Boolean:
      if (syntax.kind == detail::ValueSyntax::Kind::Boolean) {
        return ParameterValue(syntax.text == "true");
      }
      break;
    case detail::ValueKind::String:
      if (syntax.kind == detail::ValueSyntax::Kind::String) {
        return ParameterValue(syntax.text);
      }
      break;
    case detail::ValueKind::Type: {
      auto value = type(syntax);
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    case detail::ValueKind::Attribute: {
      auto value = attribute(syntax);
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    case detail::ValueKind::Function:
    case detail::ValueKind::Bytes:
      break;
    }
    report("value has the wrong kind for parameter '" + expected.name + "'",
           syntax.range);
    return std::nullopt;
  }

  std::optional<Type> reflected_type(const Module::Expression& expression) {
    const auto domain = detail::kernel_domain(expression);
    if (!domain) {
      return std::nullopt;
    }
    if (!domain->list) {
      return compiler_.make(detail::domain_name(domain->element));
    }
    if (expression.arguments.size() != 1U) {
      return std::nullopt;
    }
    auto element = reflected_type(expression.arguments.front());
    const auto prelude = compiler_.module(detail::prelude_module_name);
    const auto list = prelude ? prelude->type("list")
                              : std::optional<Module::TypeDecl>{};
    return element && list ? compiler_.make(*list, *element)
                           : std::optional<Type>{};
  }

  std::optional<Module::Expression> domain(const Type& type) const {
    const Module::Symbol symbol = type.schema().symbol();
    if (symbol.module_name() != detail::prelude_module_name) {
      return std::nullopt;
    }
    if (symbol.local_name() == "list") {
      const auto parameters = detail::TypeAccess::parameters(type);
      const Type* element = parameters.size() == 1U
                                ? parameters.front().as_type()
                                : nullptr;
      auto element_domain = element ? domain(*element) : std::nullopt;
      return element_domain
                 ? std::optional<Module::Expression>{
                       Module::Expression::list_domain(
                           std::move(*element_domain))}
                 : std::nullopt;
    }
    return detail::kernel_domain(
               Module::Expression::reference(
                   std::string(symbol.local_name())))
               ? std::optional<Module::Expression>{
                     Module::Expression::reference(
                         std::string(symbol.local_name()))}
               : std::nullopt;
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
        if (kind == Module::SymbolKind::Type ||
            kind == Module::SymbolKind::Attribute) {
          return Module::ParameterDecl{
              "result",
              detail::domain_expression(
                  kind == Module::SymbolKind::Type
                      ? detail::ValueKind::Type
                      : detail::ValueKind::Attribute),
              false, std::nullopt};
        }
      }
      auto value = use(detail::LocalUseSyntax{expression.text, range});
      auto value_domain = value ? domain(value->type()) : std::nullopt;
      return value_domain
                 ? std::optional<Module::ParameterDecl>{
                       {"result", std::move(*value_domain), false,
                        std::nullopt}}
                 : std::nullopt;
    }
    if (expression.kind == Kind::Number) {
      const bool real = expression.text.find_first_of(".eE") !=
                        std::string::npos;
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
          "result", detail::domain_expression(detail::ValueKind::String),
          false, std::nullopt};
    }
    if (expression.kind == Kind::List && !expression.arguments.empty()) {
      auto element = known_result(expression.arguments.front(), range);
      return element
                 ? std::optional<Module::ParameterDecl>{
                       {"result",
                        Module::Expression::list_domain(element->domain),
                        false, std::nullopt}}
                 : std::nullopt;
    }
    if (expression.kind == Kind::If && expression.arguments.size() == 3U) {
      return known_result(expression.arguments[1], range);
    }
    if (expression.kind == Kind::Call) {
      if (expression.text == "ceildiv" || expression.text == "min" ||
          expression.text == "max") {
        return Module::ParameterDecl{
            "result", detail::domain_expression(detail::ValueKind::Integer),
            false, std::nullopt};
      }
      auto function = declaration<Module::FunctionDecl>(expression.text, range);
      const auto results = function ? detail::parameter_results(*function)
                                    : std::vector<Module::ParameterDecl>{};
      return results.size() == 1U
                 ? std::optional<Module::ParameterDecl>{results.front()}
                 : std::nullopt;
    }
    if ((expression.kind == Kind::Prefix ||
         expression.kind == Kind::Infix ||
         expression.kind == Kind::Postfix) &&
        !expression.arguments.empty()) {
      return known_result(expression.arguments.front(), range);
    }
    return std::nullopt;
  }

  detail::KnownBindings known_bindings() const {
    detail::KnownBindings bindings;
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      for (const auto& [name, value] : *scope) {
        if (bindings.contains(name) || !value || !value->known()) {
          continue;
        }
        if (const auto payload =
                detail::FunctionAccess::known_value(*value)) {
          bindings.emplace(name, *payload);
        }
      }
    }
    return bindings;
  }

  std::optional<Value> evaluate_known(const detail::ExpressionSyntax& syntax) {
    auto expected = known_result(syntax.value, syntax.range);
    if (!expected) {
      report("cannot determine the type required by compile-time evaluation",
             syntax.range);
      return std::nullopt;
    }
    std::optional<ParameterValue> payload;
    if (syntax.value.kind == Module::Expression::Kind::Reference &&
        !syntax.value.arguments.empty()) {
      auto value = value_syntax(syntax.value, syntax.range);
      const auto kind = declaration_kind(syntax.value.text);
      if (value && kind == Module::SymbolKind::Type) {
        if (auto result = type(*value)) {
          payload = ParameterValue(*result);
        }
      } else if (value && kind == Module::SymbolKind::Attribute) {
        if (auto result = attribute(*value)) {
          payload = ParameterValue(*result);
        }
      }
    } else {
      payload = detail::evaluate_known_expression(
          compiler_, owner_, syntax.value, *expected, known_bindings(),
          diagnostics_, source(syntax.range), residual_control_depth_ == 0U);
    }
    auto type = payload ? reflected_type(expected->domain) : std::nullopt;
    return payload && type
               ? compiler_.known(std::move(*type), std::move(*payload))
               : std::optional<Value>{};
  }

  template <typename Declaration, typename Construct>
  auto construct(const detail::ValueSyntax& syntax, Construct create)
      -> decltype(create(std::declval<Declaration>(),
                         std::declval<std::vector<ParameterValue>>())) {
    using Return =
        decltype(create(std::declval<Declaration>(),
                        std::declval<std::vector<ParameterValue>>()));
    if (syntax.kind != detail::ValueSyntax::Kind::Reference) {
      report("expected a declaration reference", syntax.range);
      return Return{};
    }
    auto schema = declaration<Declaration>(syntax.text, syntax.range);
    if (!schema) {
      return Return{};
    }
    if (syntax.elements.size() > schema->parameters().size()) {
      report("too many parameters for '" + syntax.text + "'", syntax.range);
      return Return{};
    }
    std::vector<ParameterValue> parameters;
    for (std::size_t index = 0; index < syntax.elements.size(); ++index) {
      auto value =
          parameter(syntax.elements[index], schema->parameters()[index]);
      if (!value) {
        return Return{};
      }
      parameters.push_back(std::move(*value));
    }
    const std::size_t before = diagnostics_.size();
    Return value = create(*schema, std::move(parameters));
    detail::DiagnosticAccess::attach_since(diagnostics_, before,
                                           source(syntax.range));
    return value;
  }

  std::optional<Type> type(const detail::ValueSyntax& syntax) {
    return construct<Module::TypeDecl>(
        syntax, [this](const Module::TypeDecl& schema,
                       std::vector<ParameterValue> parameters) {
          return detail::CompilerAccess::make(
              compiler_, schema, std::span<const ParameterValue>(parameters));
        });
  }

  std::optional<Attribute> attribute(const detail::ValueSyntax& syntax) {
    return construct<Module::AttributeDecl>(
        syntax, [this](const Module::AttributeDecl& schema,
                       std::vector<ParameterValue> parameters) {
          return detail::CompilerAccess::make(
              compiler_, schema, std::span<const ParameterValue>(parameters));
        });
  }

  std::optional<Value> lookup(std::string_view name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(std::string(name));
      if (found != scope->end()) {
        return found->second;
      }
    }
    return std::nullopt;
  }

  std::optional<Value> use(const detail::LocalUseSyntax& use) {
    if (auto value = lookup(use.name)) {
      return value;
    }
    report("use of undefined local value '" + use.name + "'", use.range);
    return std::nullopt;
  }

  std::optional<Value> use(const detail::ExpressionSyntax& expression) {
    if ((expression.value.kind != Module::Expression::Kind::Reference &&
         expression.value.kind != Module::Expression::Kind::Variable) ||
        !expression.value.arguments.empty()) {
      report("expected a local value reference", expression.range);
      return std::nullopt;
    }
    return use(detail::LocalUseSyntax{expression.value.text, expression.range});
  }

  void define(std::string name, Value value, detail::SyntaxRange range) {
    if (!scopes_.back()
             .emplace(std::move(name), std::optional<Value>{std::move(value)})
             .second) {
      report("a local value may only be defined once", range);
    }
  }

  void bind(const detail::BindingSyntax& binding,
            std::optional<Value> value) {
    if (!binding.rebind) {
      if (!scopes_.back().emplace(binding.name, std::move(value)).second) {
        report("a local value may only be defined once", binding.range);
      }
      return;
    }
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(binding.name);
      if (found != scope->end()) {
        found->second = std::move(value);
        return;
      }
    }
    report("cannot rebind undefined local value '" + binding.name + "'",
           binding.range);
  }

  void invalidate(std::span<const detail::BindingSyntax> bindings) {
    for (const auto& binding : bindings) {
      bind(binding, std::nullopt);
    }
  }

  std::optional<Type>
  expected_type(const detail::BindingSyntax& binding) {
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
    const auto prelude = compiler_.module(detail::prelude_module_name);
    const auto literal = prelude
                             ? prelude->interface("literal")
                             : std::optional<Module::InterfaceDecl>{};
    if (!payload || !literal) {
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
        if (!compiler_.conforms(candidate, *literal) ||
            detail::parameter_inputs(candidate).size() != 1U ||
            !detail::ir_inputs(candidate).empty() ||
            !detail::parameter_results(candidate).empty() ||
            detail::ir_results(candidate).size() != 1U) {
          continue;
        }
        Diagnostics candidate_diagnostics;
        const std::array<std::optional<ParameterValue>, 1> known{payload};
        const std::array<std::optional<Type>, 1> expected{target};
        if (detail::resolve_operation_types(
                compiler_, candidate, {}, known, expected,
                candidate_diagnostics)) {
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

    Instruction operation =
        edit_->append(block, matches.front(), {value}, {target});
    detail::FunctionAccess::locate(*edit_, operation, source(range));
    return operation.result(0);
  }

  std::optional<Module::FunctionDecl>
  operator_declaration(std::string_view notation,
                       Module::FunctionDecl::Fixity fixity,
                       std::span<const Type> argument_types,
                       detail::SyntaxRange range) {
    const auto owner = compiler_.module(owner_);
    if (!owner) {
      report("cannot resolve operator '" + std::string(notation) +
                 "' without its owning module",
             range);
      return std::nullopt;
    }
    std::vector<Module> visible{*owner};
    for (const auto& import : owner->imports()) {
      if (const auto module = compiler_.module(import.name)) {
        visible.push_back(*module);
      }
    }

    std::vector<Module::FunctionDecl> matches;
    for (const Module& module : visible) {
      for (const auto& candidate : module.functions()) {
        if (candidate.operator_symbol() != notation ||
            candidate.operator_fixity() != fixity) {
          continue;
        }
        std::vector<std::optional<ParameterValue>> named_arguments(
            detail::parameter_inputs(candidate).size());
        std::vector<std::optional<Type>> expected(
            detail::ir_results(candidate).size());
        Diagnostics attempt;
        if (detail::resolve_operation_types(
                compiler_, candidate, argument_types, named_arguments, expected,
                attempt)) {
          matches.push_back(candidate);
        }
      }
    }
    if (matches.empty()) {
      report("no visible function matches operator '" +
                 std::string(notation) + "' for these argument types",
             range);
      return std::nullopt;
    }
    if (matches.size() != 1U) {
      std::string candidates;
      for (const auto& match : matches) {
        candidates += candidates.empty() ? "" : ", ";
        candidates += match.symbol().qualified_name();
      }
      report("operator '" + std::string(notation) +
                 "' is ambiguous between " + candidates,
             range);
      return std::nullopt;
    }
    return matches.front();
  }

  std::pair<Block, std::optional<Value>>
  instantiate_expression(const Module::Expression& expression,
                         detail::SyntaxRange range, Block block) {
    using Kind = Module::Expression::Kind;
    const detail::ExpressionSyntax syntax{expression, range};
    if ((expression.kind == Kind::Variable ||
         expression.kind == Kind::Reference) &&
        expression.arguments.empty()) {
      return {block, use(syntax)};
    }
    const std::string name = "$value" + std::to_string(next_temporary_++);
    detail::StatementSyntax statement;
    statement.bindings.push_back({name, std::nullopt, range});
    statement.expression = syntax;
    statement.range = range;
    Flow flow = instantiate_statement(statement, block);
    return flow.kind == Flow::Kind::Next && flow.block
               ? std::pair{*flow.block,
                           use(detail::LocalUseSyntax{name, range})}
               : std::pair{block, std::optional<Value>{}};
  }

  void collect_rebindings(
      const std::vector<detail::StatementSyntax>& statements,
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

  Flow instantiate_if_statement(const detail::StatementSyntax& statement,
                                Block block) {
    if (known_result(statement.expression.value, statement.expression.range)) {
      auto condition = evaluate_known(statement.expression);
      const auto selected = condition ? condition->get<bool>() : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               statement.expression.range);
        return Flow::next(block);
      }
      const auto& arm = *selected ? statement.body : statement.otherwise;
      scopes_.emplace_back();
      Flow flow = Flow::next(block);
      for (const auto& nested : arm) {
        if (flow.kind != Flow::Kind::Next || !flow.block) {
          break;
        }
        flow = instantiate_statement(nested, *flow.block);
      }
      scopes_.pop_back();
      return flow;
    }

    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);
    collect_rebindings(statement.otherwise, carried_names, seen);

    auto [condition_tail, condition] = instantiate_expression(
        statement.expression.value, statement.expression.range, block);
    const auto i1 = compiler_.make("i1");
    if (!condition || condition->known() || !i1 || condition->type() != *i1) {
      report("Residual if condition must have type i1",
             statement.expression.range);
      return Flow::next(block);
    }
    const Block yes = edit_->block();
    const Block no = edit_->block();
    edit_->branch(condition_tail, *condition, yes, {}, no, {});

    const auto incoming = scopes_;
    const auto elaborate = [&](const std::vector<detail::StatementSyntax>& arm,
                               Block start) {
      scopes_ = incoming;
      scopes_.emplace_back();
      Flow flow = Flow::next(start);
      for (const auto& nested : arm) {
        if (flow.kind != Flow::Kind::Next || !flow.block) {
          break;
        }
        flow = instantiate_statement(nested, *flow.block);
      }
      scopes_.pop_back();
      std::vector<std::optional<Value>> values;
      values.reserve(carried_names.size());
      for (const std::string& name : carried_names) {
        values.push_back(lookup(name));
      }
      return std::pair{flow, std::move(values)};
    };

    ++residual_control_depth_;
    auto [true_flow, true_values] = elaborate(statement.body, yes);
    auto [false_flow, false_values] = elaborate(statement.otherwise, no);
    --residual_control_depth_;
    scopes_ = incoming;

    if (true_flow.kind == Flow::Kind::Break ||
        true_flow.kind == Flow::Kind::Continue ||
        false_flow.kind == Flow::Kind::Break ||
        false_flow.kind == Flow::Kind::Continue) {
      report("loop control depending on Residual data requires a Residual "
             "loop condition",
             statement.range);
      return Flow::stop();
    }
    const bool true_continues = true_flow.kind == Flow::Kind::Next &&
                                true_flow.block.has_value();
    const bool false_continues = false_flow.kind == Flow::Kind::Next &&
                                 false_flow.block.has_value();
    if (!true_continues && !false_continues) {
      return Flow::stop();
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
        bind({carried_names[index], std::nullopt, statement.range, true},
             values[index]);
      }
      return true_continues ? true_flow : false_flow;
    }

    std::vector<Type> merge_types;
    std::vector<Value> true_arguments;
    std::vector<Value> false_arguments;
    std::vector<std::optional<Value>> unchanged(carried_names.size());
    std::vector<std::optional<std::size_t>> merged(carried_names.size());
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      if (!true_values[index] || !false_values[index]) {
        report("if arm does not produce outer binding '" +
                   carried_names[index] + "'",
               statement.range);
        continue;
      }
      if (*true_values[index] == *false_values[index]) {
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
        report("if arms assign incompatible types to '" +
                   carried_names[index] + "'",
               statement.range);
        continue;
      }
      auto true_value = materialize(*true_values[index], *target,
                                    *true_flow.block, statement.range);
      auto false_value = materialize(*false_values[index], *target,
                                     *false_flow.block, statement.range);
      if (!true_value || !false_value) {
        continue;
      }
      merged[index] = merge_types.size();
      merge_types.push_back(*target);
      true_arguments.push_back(*true_value);
      false_arguments.push_back(*false_value);
    }
    if (!ok()) {
      return Flow::next(block);
    }

    const Block merge = edit_->block(merge_types);
    edit_->jump(*true_flow.block, merge, true_arguments);
    edit_->jump(*false_flow.block, merge, false_arguments);
    const auto arguments = merge.arguments();
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      const auto value = merged[index]
                             ? std::optional<Value>{arguments[*merged[index]]}
                             : unchanged[index];
      bind({carried_names[index], std::nullopt, statement.range, true}, value);
    }
    return Flow::next(merge);
  }

  Flow instantiate_while(const detail::StatementSyntax& statement,
                         Block block) {
    if (known_result(statement.expression.value, statement.expression.range)) {
      while (ok()) {
        auto condition = evaluate_known(statement.expression);
        const auto selected = condition ? condition->get<bool>() : std::nullopt;
        if (!selected) {
          report("Known while condition must have type bool",
                 statement.expression.range);
          return Flow::next(block);
        }
        if (!*selected) {
          return Flow::next(block);
        }
        if (loop_iterations_++ >= compiler_.evaluation_limits().steps) {
          report("compile-time while iteration limit exceeded",
                 statement.range);
          return Flow::next(block);
        }
        scopes_.emplace_back();
        loops_.push_back({});
        Flow flow = Flow::next(block);
        for (const auto& nested : statement.body) {
          if (flow.kind != Flow::Kind::Next || !flow.block) {
            break;
          }
          flow = instantiate_statement(nested, *flow.block);
        }
        loops_.pop_back();
        scopes_.pop_back();
        if (flow.kind == Flow::Kind::Stop || !flow.block) {
          return Flow::stop();
        }
        block = *flow.block;
        if (flow.kind == Flow::Kind::Break) {
          return Flow::next(block);
        }
        if (flow.kind == Flow::Kind::Continue) {
          continue;
        }
      }
      return Flow::next(block);
    }

    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);

    std::vector<Value> initial;
    std::vector<Type> carried_types;
    for (const std::string& name : carried_names) {
      auto value = lookup(name);
      if (!value) {
        report("loop-carried value '" + name + "' is unavailable",
               statement.range);
        continue;
      }
      carried_types.push_back(value->type());
      auto carried = materialize(*value, value->type(), block, statement.range);
      if (carried) {
        initial.push_back(*carried);
      }
    }
    if (initial.size() != carried_names.size()) {
      return Flow::next(block);
    }

    const Block header = edit_->block(carried_types);
    const Block body = edit_->block();
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
      return Flow::next(block);
    }
    edit_->branch(condition_tail, *condition, body, {}, exit,
                  header.arguments());

    ++residual_control_depth_;
    scopes_.emplace_back();
    loops_.push_back({header, exit, carried_names, carried_types});
    Flow body_flow = Flow::next(body);
    for (const auto& nested : statement.body) {
      if (body_flow.kind != Flow::Kind::Next || !body_flow.block) {
        break;
      }
      body_flow = instantiate_statement(nested, *body_flow.block);
    }
    loops_.pop_back();
    std::vector<Value> updated;
    if (body_flow.kind == Flow::Kind::Next && body_flow.block) {
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        const std::string& name = carried_names[index];
        if (auto value = lookup(name)) {
          auto carried = materialize(*value, carried_types[index],
                                     *body_flow.block, statement.range);
          if (carried) {
            updated.push_back(*carried);
          }
        }
      }
    }
    scopes_.pop_back();
    --residual_control_depth_;
    if (body_flow.kind == Flow::Kind::Break ||
        body_flow.kind == Flow::Kind::Continue) {
      report("loop control was not consumed by its Residual loop",
             statement.range);
      return Flow::next(block);
    }
    if (body_flow.kind == Flow::Kind::Next && body_flow.block &&
        updated.size() != carried_names.size()) {
      report("while body does not produce every loop-carried value",
             statement.range);
      return Flow::next(block);
    }
    if (body_flow.kind == Flow::Kind::Next && body_flow.block) {
      edit_->jump(*body_flow.block, header, updated);
    }
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      bind({carried_names[index], std::nullopt, statement.range, true},
           exit.arguments()[index]);
    }
    return Flow::next(exit);
  }

  Flow instantiate_return(
      std::span<const detail::ExpressionSyntax> expressions,
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
          if (flow.kind == Flow::Kind::Next && flow.block) {
            block = *flow.block;
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
    return Flow::stop();
  }

  Flow instantiate_loop_control(Flow::Kind kind,
                                detail::SyntaxRange range, Block block) {
    if (loops_.empty()) {
      report("loop control is outside a structured loop", range);
      return Flow::stop();
    }
    const LoopContext& loop = loops_.back();
    const std::optional<Block>& target =
        kind == Flow::Kind::Continue ? loop.continue_target
                                    : loop.break_target;
    if (!target) {
      return Flow::transfer(kind, block);
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
      return Flow::stop();
    }
    edit_->jump(block, *target, std::move(carried));
    return Flow::stop();
  }

  Flow instantiate_statement(const detail::StatementSyntax& statement,
                             Block block) {
    if (statement.kind == detail::StatementSyntax::Kind::Return) {
      return instantiate_return(statement.values, statement.range, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Break) {
      return instantiate_loop_control(Flow::Kind::Break, statement.range,
                                      block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Continue) {
      return instantiate_loop_control(Flow::Kind::Continue, statement.range,
                                      block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::If) {
      return instantiate_if_statement(statement, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::While) {
      return instantiate_while(statement, block);
    }
    using Kind = Module::Expression::Kind;
    const Module::Expression& expression = statement.expression.value;
    if (expression.kind == Kind::Variable ||
        expression.kind == Kind::Reference) {
      if (statement.bindings.size() != 1U || !expression.arguments.empty()) {
        report("a value reference must bind exactly one value",
               statement.range);
        return Flow::next(block);
      }
      if (auto value = use(statement.expression)) {
        bind(statement.bindings.front(), std::move(*value));
      }
      return Flow::next(block);
    }
    if (expression.kind != Kind::If) {
      instantiate_operation(statement, block);
      return Flow::next(block);
    }
    if (expression.arguments.size() != 3U ||
        statement.bindings.size() != 1U) {
      report("if expression must have one condition, two values, and one "
             "result",
             statement.range);
      return Flow::next(block);
    }

    const detail::ExpressionSyntax condition_syntax{
        expression.arguments[0], statement.expression.range};
    if (known_result(condition_syntax.value, condition_syntax.range)) {
      auto condition = evaluate_known(condition_syntax);
      const auto selected = condition ? condition->get<bool>() : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               condition_syntax.range);
        return Flow::next(block);
      }
      detail::StatementSyntax selected_statement = statement;
      selected_statement.expression.value =
          expression.arguments[*selected ? 1U : 2U];
      return instantiate_statement(selected_statement, block);
    }

    auto condition = use(condition_syntax);
    if (!condition) {
      return Flow::next(block);
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
      return Flow::next(block);
    }
    auto merge_type = expected_type(statement.bindings.front());
    if (true_value->known() && false_value->known() &&
        *true_value == *false_value && !merge_type) {
      const Block merge = edit_->block();
      edit_->jump(true_tail, merge);
      edit_->jump(false_tail, merge);
      bind(statement.bindings.front(), std::move(*true_value));
      return Flow::next(merge);
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
      report("Known branch values need an expected program type for "
             "materialization",
             statement.range);
      return Flow::next(block);
    }
    if ((!true_value->known() && true_value->type() != *merge_type) ||
        (!false_value->known() && false_value->type() != *merge_type)) {
      report("if branches produce different types", statement.range);
      return Flow::next(block);
    }
    auto materialized_true =
        materialize(*true_value, *merge_type, true_tail, statement.range);
    auto materialized_false =
        materialize(*false_value, *merge_type, false_tail, statement.range);
    if (!materialized_true || !materialized_false) {
      return Flow::next(block);
    }
    const Block merge = edit_->block({*merge_type});
    edit_->jump(true_tail, merge, {*materialized_true});
    edit_->jump(false_tail, merge, {*materialized_false});
    bind(statement.bindings.front(), merge.arguments().front());
    return Flow::next(merge);
  }

  void instantiate_operation(const detail::StatementSyntax& statement,
                             Block block) {
    const bool require_known = statement.expression.value.kind ==
                               Module::Expression::Kind::Evaluate;
    const bool can_evaluate = statement.bindings.size() == 1U &&
                              known_result(statement.expression.value,
                                           statement.expression.range)
                                  .has_value();
    if (require_known || can_evaluate) {
      if (statement.bindings.size() != 1U) {
        report("compile-time evaluation must bind exactly one value",
               statement.range);
        return;
      }
      auto value = evaluate_known(statement.expression);
      if (value) {
        auto expected = expected_type(statement.bindings.front());
        if (expected && value->type() != *expected) {
          value = materialize(*value, *expected, block, statement.range);
        }
        if (value) {
          bind(statement.bindings.front(), std::move(*value));
        } else {
          invalidate(statement.bindings);
        }
      } else {
        invalidate(statement.bindings);
      }
      return;
    }
    const auto invalidate_results = [&] { invalidate(statement.bindings); };
    const Module::Expression& expression = statement.expression.value;
    using Kind = Module::Expression::Kind;
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

    std::vector<Value> arguments;
    arguments.reserve(expression.arguments.size());
    for (const Module::Expression& argument_expression : expression.arguments) {
      detail::ExpressionSyntax argument_syntax{
          argument_expression, statement.expression.range};
      std::optional<Value> argument;
      if ((argument_expression.kind == Kind::Reference ||
           argument_expression.kind == Kind::Variable) &&
          argument_expression.arguments.empty()) {
        argument = use(argument_syntax);
      } else {
        argument = evaluate_known(argument_syntax);
      }
      if (argument) {
        arguments.push_back(*argument);
      } else {
        invalidate_results();
        return;
      }
    }

    std::optional<Module::FunctionDecl> schema;
    if (fixity) {
      std::vector<Type> types;
      types.reserve(arguments.size());
      for (const Value& argument : arguments) {
        types.push_back(argument.type());
      }
      schema = operator_declaration(expression.text, *fixity, types,
                                    statement.expression.range);
    } else {
      schema = declaration<Module::FunctionDecl>(
          expression.text, statement.expression.range);
    }
    if (!schema) {
      invalidate_results();
      return;
    }

    const auto parameters = schema->inputs();
    std::vector<std::vector<Value>> bound(parameters.size());
    std::size_t positional = 0;
    bool invalid_argument = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const std::string_view label =
          index < expression.labels.size() ? expression.labels[index]
                                           : std::string_view{};
      std::size_t target = parameters.size();
      if (!label.empty()) {
        const auto found = std::find_if(
            parameters.begin(), parameters.end(),
            [&](const Module::ParameterDecl& parameter) {
              return parameter.name == label;
            });
        if (found != parameters.end()) {
          target = static_cast<std::size_t>(
              std::distance(parameters.begin(), found));
        }
      } else if (positional < parameters.size()) {
        target = positional;
        if (!parameters[target].variadic) {
          ++positional;
        }
      }
      if (target == parameters.size()) {
        report(label.empty() ? "call has too many positional arguments"
                             : "call has no argument named '" +
                                   std::string(label) + "'",
               statement.expression.range);
        invalid_argument = true;
        continue;
      }
      if (!parameters[target].variadic && !bound[target].empty()) {
        report("call provides argument '" + parameters[target].name +
                   "' more than once",
               statement.expression.range);
        invalid_argument = true;
        continue;
      }
      bound[target].push_back(arguments[index]);
    }

    const auto& contract = detail::FunctionTypeAccess::get(*schema);
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (bound[index].empty() && parameters[index].default_value) {
        auto payload = detail::parameter_default(parameters[index]);
        auto type = reflected_type(parameters[index].domain);
        auto value = payload && type
                         ? compiler_.known(std::move(*type),
                                           std::move(*payload))
                         : std::optional<Value>{};
        if (value) {
          bound[index].push_back(std::move(*value));
        }
      }
      if (bound[index].empty() && !parameters[index].variadic) {
        report("call is missing argument '" + parameters[index].name + "'",
               statement.expression.range);
        invalid_argument = true;
      }
      if (!contract.ir_inputs[index]) {
        for (const Value& value : bound[index]) {
          const auto payload = value.known()
                                   ? detail::FunctionAccess::known_value(value)
                                   : std::nullopt;
          if (!payload ||
              !detail::matches_parameter(parameters[index], *payload)) {
            report("argument '" + parameters[index].name +
                       "' must be Known and compatible",
                   statement.expression.range);
            invalid_argument = true;
          }
        }
      }
    }
    if (invalid_argument) {
      invalidate_results();
      return;
    }

    std::vector<std::optional<Type>> expected_types;
    bool invalid_expected_type = false;
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      const auto& binding = statement.bindings[index];
      if (!binding.type) {
        const auto expected = scopes_.size() == 1U
                                  ? expected_values_.find(binding.name)
                                  : expected_values_.end();
        expected_types.push_back(
            expected == expected_values_.end()
                ? std::optional<Type>{}
                : std::optional<Type>{expected->second});
        continue;
      }
      auto resolved = type(*binding.type);
      invalid_expected_type = !resolved || invalid_expected_type;
      expected_types.push_back(std::move(resolved));
    }
    if (invalid_expected_type) {
      report("operation result names and types have different counts",
             statement.range);
      invalidate_results();
      return;
    }

    std::vector<Type> argument_types;
    std::vector<std::optional<ParameterValue>> known_values(
        detail::parameter_inputs(*schema).size());
    std::size_t known_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (contract.ir_inputs[index]) {
        for (const Value& value : bound[index]) {
          argument_types.push_back(value.type());
        }
      } else {
        if (!bound[index].empty()) {
          known_values[known_index] =
              detail::FunctionAccess::known_value(bound[index].front());
        }
        ++known_index;
      }
    }
    auto resolved = detail::resolve_operation_types(
        compiler_, *schema, argument_types, known_values, expected_types,
        diagnostics_, source(statement.expression.range));
    if (!resolved) {
      invalidate_results();
      return;
    }

    std::vector<Value> call_arguments;
    for (const auto& values : bound) {
      for (const Value& value : values) {
        call_arguments.push_back(value);
      }
    }
    Instruction operation = edit_->append(
        std::move(block), *schema, std::move(call_arguments), resolved->results);
    detail::FunctionAccess::locate(
        *edit_, operation, source(statement.expression.range));
    if (statement.bindings.size() != operation.results().size()) {
      report("call result count does not match its bindings", statement.range);
      invalidate_results();
      return;
    }
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      bind(statement.bindings[index], operation.result(index));
    }
  }

  Compiler& compiler_;
  Module::FunctionDecl declaration_;
  const detail::FunctionBody& body_;
  std::string owner_;
  Diagnostics& diagnostics_;
  std::size_t initial_diagnostics_ = 0;
  std::optional<Function> function_;
  std::optional<Function::Edit> edit_;
  std::vector<
      std::unordered_map<std::string, std::optional<Value>>>
      scopes_;
  std::unordered_map<std::string, Type> expected_values_;
  std::vector<Type> result_types_;
  std::unordered_map<std::string, Block> blocks_;
  std::vector<LoopContext> loops_;
  std::size_t next_temporary_ = 0;
  std::size_t residual_control_depth_ = 0;
  std::size_t loop_iterations_ = 0;
  std::vector<Value> supplied_known_;
};

class RuntimeSyntax {
public:
  RuntimeSyntax(const Function& function, std::string_view name) : function_(function) {
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
      block_names_.emplace_back(
          blocks[index], index == 0U ? "entry" : "block" + std::to_string(index));
    }
    for (const Block& block : blocks) {
      for (const Value& argument : block.arguments()) {
        bind(argument, "arg");
      }
    }
    for (const Block& block : blocks) {
      detail::BlockSyntax syntax;
      syntax.name = block_name(block);
      for (const Value& argument : block.arguments()) {
        syntax.arguments.push_back({use(argument), value(argument.type()), {}});
      }
      for (const Instruction& instruction : block.instructions()) {
        syntax.statements.push_back(convert(instruction));
      }
      syntax.terminator = convert(block.terminator());
      syntax_.body.blocks.push_back(std::move(syntax));
    }
    return std::move(syntax_);
  }

private:
  static std::size_t visible_parameters(
      std::span<const ParameterValue> parameters,
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

  static detail::ValueSyntax value(const Type& type) {
    detail::ValueSyntax result;
    result.kind = detail::ValueSyntax::Kind::Reference;
    result.text = detail::display_type_name(
        type.schema().symbol().qualified_name());
    const auto parameters = detail::TypeAccess::parameters(type);
    const std::size_t count =
        visible_parameters(parameters, type.schema().parameters());
    for (std::size_t index = 0; index < count; ++index) {
      result.elements.push_back(value(parameters[index]));
    }
    return result;
  }

  static detail::ValueSyntax value(const Attribute& attribute) {
    detail::ValueSyntax result;
    result.kind = detail::ValueSyntax::Kind::Reference;
    result.text = attribute.schema().symbol().qualified_name();
    const auto parameters = detail::TypeAccess::parameters(attribute);
    const std::size_t count =
        visible_parameters(parameters, attribute.schema().parameters());
    for (std::size_t index = 0; index < count; ++index) {
      result.elements.push_back(value(parameters[index]));
    }
    return result;
  }

  static detail::ValueSyntax value(const ParameterValue& parameter) {
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
    case ParameterValue::Kind::Attribute:
      return value(*parameter.as_attribute());
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

  std::string use(const Value& value) const {
    const auto found =
        std::find_if(names_.begin(), names_.end(),
                     [&](const auto& entry) { return entry.first == value; });
    if (found == names_.end()) {
      throw std::invalid_argument(
          "the function contains a value before its definition");
    }
    return found->second;
  }

  std::string block_name(const Block& block) const {
    const auto found = std::find_if(
        block_names_.begin(), block_names_.end(),
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
    case detail::ValueSyntax::Kind::Number: result.kind = Kind::Number; break;
    case detail::ValueSyntax::Kind::Boolean: result.kind = Kind::Boolean; break;
    case detail::ValueSyntax::Kind::String: result.kind = Kind::String; break;
    case detail::ValueSyntax::Kind::List: result.kind = Kind::List; break;
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

  detail::StatementSyntax convert(const Instruction& operation) {
    detail::StatementSyntax result;
    result.expression.value.kind = Module::Expression::Kind::Call;
    result.expression.value.text =
        operation.callee().symbol().qualified_name();
    for (const Value& output : operation.results()) {
      result.bindings.push_back(
          {bind(output, "v"), value(output.type()), {}});
    }
    const auto arguments = operation.arguments();
    const auto parameters = operation.callee().inputs();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const Value& argument = arguments[index];
      const std::size_t parameter_index =
          detail::FunctionAccess::argument_parameter(operation, index);
      if (argument.known()) {
        const auto payload = detail::FunctionAccess::known_value(argument);
        if (!payload || parameter_index >= parameters.size()) {
          throw std::logic_error("instruction has an invalid Known argument");
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
    const auto notation = operation.callee().operator_symbol();
    const auto fixity = operation.callee().operator_fixity();
    const bool valid_arity =
        fixity && ((*fixity == Module::FunctionDecl::Fixity::Infix &&
                    operation.arguments().size() == 2U) ||
                   (*fixity != Module::FunctionDecl::Fixity::Infix &&
                    operation.arguments().size() == 1U));
    if (notation && valid_arity && result.bindings.size() == 1U &&
        detail::parameter_inputs(operation.callee()).empty()) {
      result.expression.value.text = std::string(*notation);
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
  std::vector<std::pair<Block, std::string>> block_names_;
  std::size_t next_value_ = 0;
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

  const std::size_t forms = static_cast<std::size_t>(!body.blocks.empty()) +
                            static_cast<std::size_t>(!body.rules.empty());
  if (forms != 1U) {
    report("a function definition must have exactly one body", body.range);
    return false;
  }
  if (body.blocks.empty()) {
    return diagnostics.size() == initial_diagnostics;
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
  const auto falls_through = [&](const auto& self,
                                 const std::vector<StatementSyntax>& statements)
      -> bool {
    for (const StatementSyntax& statement : statements) {
      if (statement.kind == StatementSyntax::Kind::Return ||
          statement.kind == StatementSyntax::Kind::Break ||
          statement.kind == StatementSyntax::Kind::Continue) {
        return false;
      }
      if (statement.kind == StatementSyntax::Kind::If && statement.has_else &&
          !self(self, statement.body) &&
          !self(self, statement.otherwise)) {
        return false;
      }
    }
    return true;
  };
  const auto check_statements = [&](const auto& self,
                                    const std::vector<StatementSyntax>& statements,
                                    std::unordered_set<std::string_view>& names)
      -> void {
    bool reachable = true;
    for (const StatementSyntax& statement : statements) {
      if (!reachable) {
        report("unreachable statement after a control transfer",
               statement.range);
      }
      if (statement.kind != StatementSyntax::Kind::Expression) {
        auto nested = names;
        self(self, statement.body, nested);
        if (statement.kind == StatementSyntax::Kind::If) {
          nested = names;
          self(self, statement.otherwise, nested);
        }
        if (statement.kind == StatementSyntax::Kind::Return ||
            statement.kind == StatementSyntax::Kind::Break ||
            statement.kind == StatementSyntax::Kind::Continue ||
            (statement.kind == StatementSyntax::Kind::If &&
             statement.has_else && !falls_through(falls_through,
                                                   statement.body) &&
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
        report("duplicate local value '" + argument.name + "'",
               argument.range);
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
    Lexer& lexer, Token& current, Diagnostics& diagnostics,
    std::string source,
    std::span<const Module::FunctionDecl::GenericDecl> variables) {
  return SyntaxParser(lexer, current, diagnostics, std::move(source), variables)
      .parse();
}

std::string format_function_body(const FunctionBody& body,
                                 std::size_t indent) {
  return SyntaxWriter(body, indent).write();
}

std::string format_function_syntax(const FunctionSyntax& function,
                                   std::size_t indent) {
  return SyntaxWriter(function, indent).write();
}

std::optional<Function> instantiate_function(Compiler& compiler,
                                          Module::FunctionDecl function,
                                          const FunctionBody& body,
                                          Diagnostics& diagnostics,
                                          std::vector<Value> known_arguments) {
  return Instantiator(compiler, std::move(function), body, diagnostics,
                      std::move(known_arguments))
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
  return detail::format_function_syntax(RuntimeSyntax(function, name).build(), 0U);
}

}  // namespace joggle
