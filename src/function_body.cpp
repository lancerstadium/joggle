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
        variables_(variables.begin(), variables.end()) {}

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
      while (!is_terminator() && !is(TokenKind::RightBrace) &&
             !is(TokenKind::End) && ok()) {
        entry.instructions.push_back(parse_statement());
      }
      if (!is_terminator()) {
        error("a function body must end with 'return'");
      } else {
        entry.terminator = parse_terminator();
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
      terminator.kind = detail::TerminatorSyntax::Kind::Branch;
      terminator.successors.push_back(parse_successor());
    } else if (match_name("branch")) {
      terminator.kind = detail::TerminatorSyntax::Kind::CondBranch;
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
        block.arguments.push_back(std::move(argument));
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
    expect(TokenKind::Colon, "':'");
    while (!is_terminator() && !looks_like_block_header() &&
           !is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
      block.instructions.push_back(parse_statement());
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
    if (starts_binding()) {
      auto add_binding = [&](std::string name, SourcePosition binding_begin) {
        detail::BindingSyntax binding;
        binding.name = std::move(name);
        if (match(TokenKind::Colon)) {
          binding.type = parse_value();
        }
        binding.range = {binding_begin, previous_end_};
        variables_.push_back(
            {binding.name, Module::Expression::reference("type"),
             std::nullopt});
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
        body_->blocks.front().terminator.kind ==
            detail::TerminatorSyntax::Kind::Return;
    if (straight_line) {
      const detail::BlockSyntax& entry = body_->blocks.front();
      for (const auto& instruction : entry.instructions) {
        write_statement(instruction, indent_ + 1U);
      }
      write_terminator(entry.terminator, indent_ + 1U);
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
        for (const auto& instruction : block.instructions) {
          write_statement(instruction, indent_ + 2U);
        }
        write_terminator(block.terminator, indent_ + 2U);
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
    case detail::TerminatorSyntax::Kind::Branch:
      output_ << "jump ";
      if (!terminator.successors.empty()) {
        write_successor(terminator.successors.front());
      }
      break;
    case detail::TerminatorSyntax::Kind::CondBranch:
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

// Adapter used only while the old Function bridge is being removed. The source
// AST above keeps one Expression; this shape exists solely to feed the legacy
// operation constructor without making Call a second syntax node again.
struct ResidualPropertySyntax {
  std::string name;
  detail::ValueSyntax value;
  detail::SyntaxRange range;
};

struct ResidualCallSyntax {
  std::vector<std::string> results;
  std::vector<std::optional<detail::ValueSyntax>> result_types;
  std::string operation;
  std::optional<Module::FunctionDecl::Fixity> operator_fixity;
  std::vector<detail::LocalUseSyntax> arguments;
  std::vector<ResidualPropertySyntax> properties;
  detail::SyntaxRange range;
};

class Instantiator {
public:
  Instantiator(Compiler& compiler, Module::FunctionDecl function,
               const detail::FunctionBody& body, Diagnostics& diagnostics)
      : compiler_(compiler), declaration_(std::move(function)), body_(body),
        owner_(declaration_.symbol().module_name()),
        diagnostics_(diagnostics), initial_diagnostics_(diagnostics.size()) {}

  std::optional<Function> instantiate() {
    function_ = compiler_.function();
    if (!function_) {
      return std::nullopt;
    }
    std::vector<Type> result_types;
    const auto results = detail::ir_results(declaration_);
    for (const auto& result : results) {
      auto syntax = value_syntax(result.domain, body_.range);
      if (!syntax) {
        report("a residual result type cannot yet be instantiated",
               body_.range);
        continue;
      }
      if (auto result_type = type(*syntax)) {
        result_types.push_back(*result_type);
      }
    }
    for (const detail::BlockSyntax& block : body_.blocks) {
      if (block.terminator.kind == detail::TerminatorSyntax::Kind::Return &&
          block.terminator.values.size() == result_types.size()) {
        for (std::size_t index = 0; index < block.terminator.values.size();
             ++index) {
          const auto& expression = block.terminator.values[index];
          if ((expression.value.kind != Module::Expression::Kind::Reference &&
               expression.value.kind != Module::Expression::Kind::Variable) ||
              !expression.value.arguments.empty()) {
            continue;
          }
          const auto [found, inserted] = expected_values_.emplace(
              expression.value.text, result_types[index]);
          if (!inserted && found->second != result_types[index]) {
            report("one returned value is constrained to different types",
                   expression.range);
          }
        }
      }
    }
    std::vector<Type> argument_types;
    const auto inputs = detail::ir_inputs(declaration_);
    for (const auto& argument : inputs) {
      auto syntax = value_syntax(argument.domain, body_.range);
      if (!syntax) {
        report("a residual input type cannot yet be instantiated", body_.range);
        continue;
      }
      auto argument_type = type(*syntax);
      if (argument_type) {
        argument_types.push_back(*argument_type);
      }
    }
    if (argument_types.size() != inputs.size() ||
        result_types.size() != results.size()) {
      return std::nullopt;
    }

    detail::FunctionAccess::declare(*function_, declaration_, argument_types,
                                    result_types);
    edit_.emplace(function_->edit());
    scopes_.emplace_back();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      define(inputs[index].name, edit_->argument(argument_types[index]),
             body_.range);
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
      for (const auto& instruction : block.instructions) {
        instantiate_operation(instruction, ir->second);
      }
      const detail::TerminatorSyntax& terminator = block.terminator;
      if (terminator.kind == detail::TerminatorSyntax::Kind::Return) {
        std::vector<Value> returned;
        if (terminator.values.size() != results.size()) {
          report("function return count does not match its result signature",
                 terminator.range);
        } else if (result_types.size() == results.size()) {
          for (std::size_t index = 0; index < terminator.values.size(); ++index) {
            auto value = use(terminator.values[index]);
            if (!value) {
              continue;
            }
            if (value->type() != result_types[index]) {
              report("returned value type does not match function result " +
                         std::to_string(index),
                     terminator.values[index].range);
              continue;
            }
            returned.push_back(*value);
          }
        }
        edit_->ret(ir->second, std::move(returned));
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
      if (terminator.kind == detail::TerminatorSyntax::Kind::Branch) {
        if (auto destination = target(0)) {
          edit_->jump(ir->second, *destination,
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
        edit_->branch(ir->second, *condition, *true_target,
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
  std::optional<ResidualCallSyntax>
  residual_call(const detail::StatementSyntax& statement) {
    ResidualCallSyntax result;
    result.range = statement.range;
    for (const detail::BindingSyntax& binding : statement.bindings) {
      result.results.push_back(binding.name);
      result.result_types.push_back(binding.type);
    }

    const Module::Expression& expression = statement.expression.value;
    using Kind = Module::Expression::Kind;
    if (expression.kind == Kind::Call) {
      result.operation = expression.text;
    } else if (expression.kind == Kind::Prefix) {
      result.operation = expression.text;
      result.operator_fixity = Module::FunctionDecl::Fixity::Prefix;
    } else if (expression.kind == Kind::Infix) {
      result.operation = expression.text;
      result.operator_fixity = Module::FunctionDecl::Fixity::Infix;
    } else if (expression.kind == Kind::Postfix) {
      result.operation = expression.text;
      result.operator_fixity = Module::FunctionDecl::Fixity::Postfix;
    } else {
      report("residual expression evaluation is not yet available in the "
             "Function bridge",
             statement.expression.range);
      return std::nullopt;
    }

    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      const Module::Expression& argument = expression.arguments[index];
      const std::string_view label =
          index < expression.labels.size() ? expression.labels[index]
                                           : std::string_view{};
      if (!label.empty()) {
        auto converted = value_syntax(argument, statement.expression.range);
        if (!converted) {
          report("named argument '" + std::string(label) +
                     "' is not known in the Function bridge",
                 statement.expression.range);
          return std::nullopt;
        }
        result.properties.push_back(
            {std::string(label), std::move(*converted),
             statement.expression.range});
        continue;
      }
      if ((argument.kind != Kind::Reference &&
           argument.kind != Kind::Variable) ||
          !argument.arguments.empty()) {
        report("a residual call argument is not a local value",
               statement.expression.range);
        return std::nullopt;
      }
      result.arguments.push_back(
          {argument.text, statement.expression.range});
    }
    return result;
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

  std::optional<ParameterValue>
  parameter(const detail::ValueSyntax& syntax,
            const Module::ParameterDecl& expected) {
    const auto domain = detail::kernel_domain(expected.domain);
    if (!domain) {
      report("unknown parameter domain for '" + expected.name + "'",
             syntax.range);
      return std::nullopt;
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

  std::optional<Value> use(const detail::LocalUseSyntax& use) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(use.name);
      if (found != scope->end()) {
        return found->second;
      }
    }
    report("use of undefined local value '" + use.name + "'", use.range);
    return std::nullopt;
  }

  std::optional<Value> use(const detail::ExpressionSyntax& expression) {
    if ((expression.value.kind != Module::Expression::Kind::Reference &&
         expression.value.kind != Module::Expression::Kind::Variable) ||
        !expression.value.arguments.empty()) {
      report("residual expression evaluation is not yet available in the "
             "Function bridge",
             expression.range);
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

  void invalidate(std::span<const std::string> names,
                  detail::SyntaxRange range) {
    for (const std::string& name : names) {
      if (!scopes_.back().emplace(name, std::nullopt).second) {
        report("a local value may only be defined once", range);
      }
    }
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
        std::vector<std::optional<ParameterValue>> properties(
            detail::parameter_inputs(candidate).size());
        std::vector<std::optional<Type>> expected(
            detail::ir_results(candidate).size());
        Diagnostics attempt;
        if (detail::resolve_operation_types(
                compiler_, candidate, argument_types, properties, expected,
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

  void instantiate_operation(const detail::StatementSyntax& statement,
                             Block block) {
    auto lowered = residual_call(statement);
    if (!lowered) {
      std::vector<std::string> results;
      results.reserve(statement.bindings.size());
      for (const auto& binding : statement.bindings) {
        results.push_back(binding.name);
      }
      invalidate(results, statement.range);
      return;
    }
    const ResidualCallSyntax& syntax = *lowered;
    auto schema = syntax.operator_fixity
                      ? std::optional<Module::FunctionDecl>{}
                      : declaration<Module::FunctionDecl>(syntax.operation,
                                                           syntax.range);
    std::vector<Value> arguments;
    bool invalid_call_argument = false;
    for (const auto& argument_syntax : syntax.arguments) {
      auto argument = use(argument_syntax);
      if (argument) {
        arguments.push_back(*argument);
      } else {
        invalid_call_argument = true;
      }
    }
    if (invalid_call_argument) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    if (syntax.operator_fixity) {
      std::vector<Type> types;
      types.reserve(arguments.size());
      for (const Value& argument : arguments) {
        types.push_back(argument.type());
      }
      schema = operator_declaration(syntax.operation, *syntax.operator_fixity,
                                    types, syntax.range);
    }
    if (!schema) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    std::vector<std::optional<Type>> expected_types;
    bool invalid_expected_type = false;
    for (const auto& result_type : syntax.result_types) {
      if (!result_type) {
        const std::size_t index = expected_types.size();
        const auto expected = scopes_.size() == 1U
                                  ? expected_values_.find(syntax.results[index])
                                  : expected_values_.end();
        expected_types.push_back(
            expected == expected_values_.end()
                ? std::optional<Type>{}
                : std::optional<Type>{expected->second});
        continue;
      }
      auto resolved = type(*result_type);
      invalid_expected_type = !resolved || invalid_expected_type;
      expected_types.push_back(std::move(resolved));
    }
    if (syntax.results.size() != expected_types.size() ||
        invalid_expected_type) {
      report("operation result names and types have different counts",
             syntax.range);
      invalidate(syntax.results, syntax.range);
      return;
    }
    std::vector<Type> argument_types;
    argument_types.reserve(arguments.size());
    for (const Value& argument : arguments) {
      argument_types.push_back(argument.type());
    }
    std::vector<std::optional<ParameterValue>> property_values(
        detail::parameter_inputs(*schema).size());
    const auto static_inputs = detail::parameter_inputs(*schema);
    bool invalid_property = false;
    for (const auto& property : syntax.properties) {
      const auto input = std::find_if(
          static_inputs.begin(), static_inputs.end(),
          [&](const Module::ParameterDecl& parameter) {
            return parameter.name == property.name;
          });
      if (input == static_inputs.end()) {
        report("unknown property '" + property.name + "' on operation '" +
                   syntax.operation + "'",
               property.range);
        invalid_property = true;
        continue;
      }
      auto value = parameter(property.value, *input);
      invalid_property = !value || invalid_property;
      if (value) {
        const std::size_t index = static_cast<std::size_t>(
            std::distance(static_inputs.begin(), input));
        property_values[index] = std::move(*value);
      }
    }
    if (invalid_property) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    auto resolved = detail::resolve_operation_types(
        compiler_, *schema, argument_types, property_values, expected_types,
        diagnostics_,
        source(syntax.range));
    if (!resolved) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    Instruction operation =
        edit_->append(std::move(block), *schema, std::move(arguments),
                      resolved->results);
    detail::FunctionAccess::locate(*edit_, operation, source(syntax.range));

    for (std::size_t index = 0; index < property_values.size(); ++index) {
      if (property_values[index]) {
        edit_->set(operation, static_inputs[index].name,
                   std::move(*property_values[index]));
      }
    }
    for (std::size_t index = 0; index < syntax.results.size(); ++index) {
      define(syntax.results[index], operation.result(index), syntax.range);
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
  std::unordered_map<std::string, Block> blocks_;
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
        syntax.instructions.push_back(convert(instruction));
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
    for (const Value& argument : operation.arguments()) {
      result.expression.value.arguments.push_back(
          Module::Expression::reference(use(argument)));
      result.expression.value.labels.emplace_back();
    }
    for (const Module::ParameterDecl& parameter :
         detail::parameter_inputs(operation.callee())) {
      if (const auto property =
              detail::FunctionAccess::property(operation, parameter.name)) {
        result.expression.value.arguments.push_back(
            expression(value(*property)));
        result.expression.value.labels.push_back(parameter.name);
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
                      ? detail::TerminatorSyntax::Kind::Branch
                      : detail::TerminatorSyntax::Kind::CondBranch;
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
    for (const StatementSyntax& instruction : block.instructions) {
      for (const BindingSyntax& binding : instruction.bindings) {
        if (!definitions.insert(binding.name).second) {
          report("duplicate local value '" + binding.name + "'",
                 instruction.range);
        }
      }
    }
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
    const TerminatorSyntax& terminator = block.terminator;
    switch (terminator.kind) {
    case TerminatorSyntax::Kind::Return:
      if (terminator.condition || !terminator.successors.empty()) {
        report("return cannot have a condition or successor", terminator.range);
      }
      break;
    case TerminatorSyntax::Kind::Branch:
      if (terminator.condition || !terminator.values.empty() ||
          terminator.successors.size() != 1U) {
        report("jump must have exactly one successor", terminator.range);
      }
      break;
    case TerminatorSyntax::Kind::CondBranch:
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
      for (const SuccessorSyntax& successor : block->terminator.successors) {
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
                                          Diagnostics& diagnostics) {
  return Instantiator(compiler, std::move(function), body, diagnostics)
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
