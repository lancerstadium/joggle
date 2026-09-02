#include "graph_member.h"

#include "compiler_internal.h"

#include "diagnostic_internal.h"
#include "graph_internal.h"
#include "joggle/compiler.h"
#include "syntax_lexer.h"
#include "type_contract.h"
#include "type_internal.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
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
               std::string source)
      : lexer_(lexer), diagnostics_(diagnostics), source_(std::move(source)),
        initial_diagnostics_(diagnostics.size()), current_(current) {}

  std::optional<detail::GraphSyntax> parse() {
    const SourcePosition begin = current_.begin;
    expect_name("graph");
    auto graph_name = name("a graph name");
    detail::GraphSyntax graph;
    graph.source = source_;
    if (graph_name) {
      graph.name = std::move(*graph_name);
    }
    graph.arguments = parse_arguments();
    if (match(TokenKind::Arrow)) {
      graph.result_types = parse_type_list();
    }
    expect(TokenKind::LeftBrace, "'{'");
    while (!is_name("return") && !is(TokenKind::RightBrace) &&
           !is(TokenKind::End) && ok()) {
      graph.operations.push_back(parse_operation());
    }
    if (!match_name("return")) {
      error("a graph must end with 'return'");
    } else {
      if (!is(TokenKind::Semicolon)) {
        do {
          const SourcePosition use_begin = current_.begin;
          if (auto value = value_name()) {
            graph.returns.push_back(
                {std::move(*value), {use_begin, previous_end_}});
          }
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::Semicolon, "';'");
    }
    if (!is(TokenKind::RightBrace)) {
      error("expected '}'");
    } else {
      graph.range = {begin, current_.end};
      advance();
    }
    return ok() && !graph.name.empty()
               ? std::optional<detail::GraphSyntax>{std::move(graph)}
               : std::nullopt;
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

  std::optional<std::string> value_name() {
    expect(TokenKind::Percent, "'%'");
    if (!is(TokenKind::Name) && !is(TokenKind::Integer)) {
      error("expected an SSA value name");
      return std::nullopt;
    }
    std::string result = current_.text;
    advance();
    return result;
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
    if (is(TokenKind::Integer) || is(TokenKind::Number)) {
      detail::ValueSyntax result{
          detail::ValueSyntax::Kind::Number, current_.text, {}, {}};
      advance();
      result.range = {begin, previous_end_};
      return result;
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

  std::vector<detail::GraphArgumentSyntax> parse_arguments() {
    std::vector<detail::GraphArgumentSyntax> result;
    if (!match(TokenKind::LeftParen)) {
      error("expected '(' after graph name");
      return result;
    }
    if (!match(TokenKind::RightParen)) {
      do {
        const SourcePosition begin = current_.begin;
        auto argument_name = value_name();
        expect(TokenKind::Colon, "':'");
        auto type = parse_value();
        if (argument_name) {
          result.push_back({std::move(*argument_name),
                            std::move(type),
                            {begin, previous_end_}});
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
    return result;
  }

  std::vector<detail::ValueSyntax> parse_type_list() {
    std::vector<detail::ValueSyntax> result;
    if (match(TokenKind::LeftParen)) {
      if (!match(TokenKind::RightParen)) {
        do {
          result.push_back(parse_value());
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
    } else {
      result.push_back(parse_value());
    }
    return result;
  }

  detail::GraphRegionSyntax parse_region() {
    detail::GraphRegionSyntax region;
    if (auto region_name = name("a region name")) {
      region.name = std::move(*region_name);
    }
    if (is(TokenKind::LeftParen)) {
      region.arguments = parse_arguments();
    }
    expect(TokenKind::LeftBrace, "'{'");
    while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
      region.operations.push_back(parse_operation());
    }
    expect(TokenKind::RightBrace, "'}'");
    return region;
  }

  detail::GraphOperationSyntax parse_operation() {
    detail::GraphOperationSyntax operation;
    const SourcePosition begin = current_.begin;
    if (is(TokenKind::Percent)) {
      do {
        if (auto result = value_name()) {
          operation.results.push_back(std::move(*result));
        }
        if (match(TokenKind::Colon)) {
          operation.result_types.push_back(parse_value());
        } else {
          operation.result_types.push_back(std::nullopt);
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::Equal, "'='");
    }
    if (auto symbol = reference("an operation name")) {
      operation.operation = std::move(*symbol);
    }
    expect(TokenKind::LeftParen, "'('");
    std::set<std::string, std::less<>> property_names;
    if (!match(TokenKind::RightParen)) {
      do {
        if (is(TokenKind::Percent)) {
          const SourcePosition use_begin = current_.begin;
          if (auto operand = value_name()) {
            operation.operands.push_back(
                {std::move(*operand), {use_begin, previous_end_}});
          }
          continue;
        }
        const SourcePosition property_begin = current_.begin;
        auto property = name("a property name");
        expect(TokenKind::Equal, "'='");
        auto value = parse_value();
        if (property) {
          if (!property_names.insert(*property).second) {
            error("duplicate property '" + *property + "'");
          }
          operation.properties.push_back({std::move(*property),
                                          std::move(value),
                                          {property_begin, previous_end_}});
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
    if (match(TokenKind::LeftBrace)) {
      while (is(TokenKind::Name) && ok()) {
        operation.regions.push_back(parse_region());
      }
      expect(TokenKind::RightBrace, "'}'");
    }
    expect(TokenKind::Semicolon, "';'");
    operation.range = {begin, previous_end_};
    return operation;
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
  SyntaxWriter(const detail::GraphSyntax& graph, std::size_t indent)
      : graph_(graph), indent_(indent) {}

  std::string write() {
    output_ << spaces(indent_) << "graph " << graph_.name;
    write_arguments(graph_.arguments);
    if (!graph_.result_types.empty()) {
      output_ << " -> ";
      write_type_list(graph_.result_types);
    }
    output_ << " {\n";
    for (const auto& operation : graph_.operations) {
      write_operation(operation, indent_ + 1U);
    }
    output_ << spaces(indent_ + 1U) << "return";
    for (std::size_t index = 0; index < graph_.returns.size(); ++index) {
      output_ << (index == 0U ? " %" : ", %") << graph_.returns[index].name;
    }
    output_ << ";\n";
    output_ << spaces(indent_) << "}\n";
    return output_.str();
  }

private:
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
  write_arguments(const std::vector<detail::GraphArgumentSyntax>& arguments) {
    output_ << '(';
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      output_ << '%' << arguments[index].name << ": ";
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

  void write_operation(const detail::GraphOperationSyntax& operation,
                       std::size_t level) {
    std::size_t prefix_width = operation.operation.size() + 2U;
    for (std::size_t index = 0; index < operation.results.size(); ++index) {
      prefix_width += operation.results[index].size() + 1U;
      if (operation.result_types[index]) {
        prefix_width += 2U + value_width(*operation.result_types[index]);
      }
      prefix_width += index == 0U ? 3U : 2U;
    }
    std::size_t argument_width = 0;
    std::size_t argument_count = 0;
    for (const auto& operand : operation.operands) {
      argument_width +=
          operand.name.size() + 1U + (argument_count++ == 0U ? 0U : 2U);
    }
    for (const auto& property : operation.properties) {
      argument_width += property.name.size() + 2U +
                        value_width(property.value) +
                        (argument_count++ == 0U ? 0U : 2U);
    }
    constexpr std::size_t line_limit = 88U;
    const bool multiline_arguments =
        argument_count > 1U &&
        level * 2U + prefix_width + argument_width > line_limit;

    output_ << spaces(level);
    for (std::size_t index = 0; index < operation.results.size(); ++index) {
      if (index != 0U) {
        output_ << ", ";
      }
      output_ << '%' << operation.results[index];
      if (operation.result_types[index]) {
        output_ << ": ";
        write_value(*operation.result_types[index]);
      }
    }
    if (!operation.results.empty()) {
      output_ << " = ";
    }
    output_ << operation.operation << '(';
    bool first = true;
    for (const auto& operand : operation.operands) {
      if (!first) {
        output_ << (multiline_arguments ? ",\n" : ", ");
      } else if (multiline_arguments) {
        output_ << '\n';
      }
      first = false;
      if (multiline_arguments) {
        output_ << spaces(level + 1U);
      }
      output_ << '%' << operand.name;
    }
    for (const auto& property : operation.properties) {
      if (!first) {
        output_ << (multiline_arguments ? ",\n" : ", ");
      } else if (multiline_arguments) {
        output_ << '\n';
      }
      first = false;
      if (multiline_arguments) {
        output_ << spaces(level + 1U);
      }
      output_ << property.name << " = ";
      write_value(property.value);
    }
    if (multiline_arguments) {
      output_ << '\n' << spaces(level);
    }
    output_ << ')';
    if (!operation.regions.empty()) {
      output_ << " {\n";
      for (const auto& region : operation.regions) {
        output_ << spaces(level + 1U) << region.name;
        if (!region.arguments.empty()) {
          write_arguments(region.arguments);
        }
        output_ << " {\n";
        for (const auto& nested : region.operations) {
          write_operation(nested, level + 2U);
        }
        output_ << spaces(level + 1U) << "}\n";
      }
      output_ << spaces(level) << '}';
    }
    output_ << ";\n";
  }

  const detail::GraphSyntax& graph_;
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
public:
  Instantiator(Compiler& compiler, const detail::GraphSyntax& syntax,
               std::string_view owner, Diagnostics& diagnostics)
      : compiler_(compiler), syntax_(syntax), owner_(owner),
        diagnostics_(diagnostics), initial_diagnostics_(diagnostics.size()) {}

  std::optional<Graph> instantiate() {
    graph_ = compiler_.graph();
    if (!graph_) {
      return std::nullopt;
    }
    edit_.emplace(graph_->edit());
    scopes_.emplace_back();
    std::vector<Type> result_types;
    for (const auto& syntax : syntax_.result_types) {
      if (auto result_type = type(syntax)) {
        result_types.push_back(*result_type);
      }
    }
    if (syntax_.returns.size() == result_types.size()) {
      for (std::size_t index = 0; index < syntax_.returns.size(); ++index) {
        const auto [found, inserted] = expected_values_.emplace(
            syntax_.returns[index].name, result_types[index]);
        if (!inserted && found->second != result_types[index]) {
          report("one returned value is constrained to different types",
                 syntax_.returns[index].range);
        }
      }
    }
    for (const auto& argument : syntax_.arguments) {
      auto argument_type = type(argument.type);
      if (argument_type) {
        define(argument.name, edit_->argument(*argument_type), argument.range);
      }
    }
    for (const auto& operation : syntax_.operations) {
      instantiate_operation(operation, detail::GraphAccess::root(*graph_));
    }
    if (syntax_.returns.size() != syntax_.result_types.size()) {
      report("graph return count does not match its result signature",
             syntax_.range);
    } else if (result_types.size() == syntax_.result_types.size()) {
      for (std::size_t index = 0; index < syntax_.returns.size(); ++index) {
        auto value = use(syntax_.returns[index]);
        if (!value) {
          continue;
        }
        if (value->type() != result_types[index]) {
          report("returned value type does not match graph result " +
                     std::to_string(index),
                 syntax_.returns[index].range);
          continue;
        }
        edit_->output(*value);
      }
    }
    if (!ok() || !edit_->commit(diagnostics_)) {
      return std::nullopt;
    }
    edit_.reset();
    return compiler_.verify(*graph_) ? std::move(graph_) : std::nullopt;
  }

private:
  bool ok() const { return diagnostics_.size() == initial_diagnostics_; }

  SourceRange source(detail::SyntaxRange range) const {
    return {syntax_.source, range.begin, range.end};
  }

  void report(std::string message, detail::SyntaxRange range) {
    diagnostics_.report(std::move(message), source(range));
  }

  std::optional<std::string_view>
  resolve_prefix(std::string_view from, std::string_view prefix) const {
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
    const auto [prefix, local] = split_reference(scope, reference);
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
      result = module->operation(local);
    }
    if (!result) {
      constexpr std::string_view kind =
          std::is_same_v<Declaration, Module::TypeDecl>
              ? "type"
              : (std::is_same_v<Declaration, Module::AttributeDecl>
                     ? "attribute"
                     : "operation");
      report("unknown " + std::string(kind) + " '" + std::string(reference) +
                 "'",
             range);
    }
    return result;
  }

  std::optional<ParameterValue>
  parameter(const detail::ValueSyntax& syntax,
            const Module::ParameterDecl& expected) {
    if (expected.list) {
      if (syntax.kind != detail::ValueSyntax::Kind::List) {
        report("expected a list value for parameter '" + expected.name + "'",
               syntax.range);
        return std::nullopt;
      }
      std::vector<ParameterValue> elements;
      for (const auto& element : syntax.elements) {
        Module::ParameterDecl scalar = expected;
        scalar.list = false;
        auto value = parameter(element, scalar);
        if (!value) {
          return std::nullopt;
        }
        elements.push_back(std::move(*value));
      }
      return ParameterValue::list(std::move(elements));
    }
    switch (expected.kind) {
    case Module::ParameterKind::I64: {
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
    case Module::ParameterKind::F64: {
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
    case Module::ParameterKind::Boolean:
      if (syntax.kind == detail::ValueSyntax::Kind::Boolean) {
        return ParameterValue(syntax.text == "true");
      }
      break;
    case Module::ParameterKind::String:
      if (syntax.kind == detail::ValueSyntax::Kind::String) {
        return ParameterValue(syntax.text);
      }
      break;
    case Module::ParameterKind::Type: {
      auto value = type(syntax);
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    case Module::ParameterKind::Attribute: {
      auto value = attribute(syntax);
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    case Module::ParameterKind::Value:
    case Module::ParameterKind::Region:
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

  std::optional<Value> use(const detail::GraphUseSyntax& use) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(use.name);
      if (found != scope->end()) {
        return found->second;
      }
    }
    report("use of undefined SSA value '%" + use.name + "'", use.range);
    return std::nullopt;
  }

  void define(std::string name, Value value, detail::SyntaxRange range) {
    if (!scopes_.back()
             .emplace(std::move(name), std::optional<Value>{std::move(value)})
             .second) {
      report("duplicate SSA value name", range);
    }
  }

  void invalidate(std::span<const std::string> names,
                  detail::SyntaxRange range) {
    for (const std::string& name : names) {
      if (!scopes_.back().emplace(name, std::nullopt).second) {
        report("duplicate SSA value name", range);
      }
    }
  }

  void instantiate_operation(const detail::GraphOperationSyntax& syntax,
                             Region region) {
    auto schema =
        declaration<Module::OperationDecl>(syntax.operation, syntax.range);
    if (!schema) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    std::vector<Value> operands;
    bool invalid_operand = false;
    for (const auto& operand_syntax : syntax.operands) {
      auto operand = use(operand_syntax);
      if (operand) {
        operands.push_back(*operand);
      } else {
        invalid_operand = true;
      }
    }
    if (invalid_operand) {
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
    std::vector<Type> operand_types;
    operand_types.reserve(operands.size());
    for (const Value& operand : operands) {
      operand_types.push_back(operand.type());
    }
    std::vector<std::optional<ParameterValue>> property_values(
        schema->inputs().size());
    bool invalid_property = false;
    for (const auto& property : syntax.properties) {
      const auto input = std::find_if(
          schema->inputs().begin(), schema->inputs().end(),
          [&](const Module::ParameterDecl& parameter) {
            return parameter.name == property.name &&
                   parameter.kind != Module::ParameterKind::Value &&
                   parameter.kind != Module::ParameterKind::Region;
          });
      if (input == schema->inputs().end()) {
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
            std::distance(schema->inputs().begin(), input));
        property_values[index] = std::move(*value);
      }
    }
    if (invalid_property) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    auto result_types = detail::infer_operation_types(
        compiler_, *schema, operand_types, property_values, expected_types,
        diagnostics_,
        source(syntax.range));
    if (!result_types) {
      invalidate(syntax.results, syntax.range);
      return;
    }
    Operation operation =
        edit_->append(region, *schema, std::move(operands), *result_types);
    detail::GraphAccess::locate(*edit_, operation, source(syntax.range));

    for (std::size_t index = 0; index < property_values.size(); ++index) {
      if (property_values[index]) {
        edit_->set(operation, schema->inputs()[index].name,
                   std::move(*property_values[index]));
      }
    }
    for (std::size_t index = 0; index < syntax.results.size(); ++index) {
      define(syntax.results[index], operation.result(index), syntax.range);
    }
    for (const auto& region_syntax : syntax.regions) {
      std::vector<Type> argument_types;
      bool valid_arguments = true;
      for (const auto& argument : region_syntax.arguments) {
        const auto argument_type = type(argument.type);
        valid_arguments = argument_type.has_value() && valid_arguments;
        if (argument_type) {
          argument_types.push_back(*argument_type);
        }
      }
      if (!valid_arguments) {
        continue;
      }
      Region nested_region = edit_->region(operation, region_syntax.name,
                                           std::move(argument_types));
      const auto arguments = nested_region.arguments();
      scopes_.emplace_back();
      for (std::size_t index = 0; index < region_syntax.arguments.size();
           ++index) {
        define(region_syntax.arguments[index].name, arguments[index],
               region_syntax.arguments[index].range);
      }
      for (const auto& nested : region_syntax.operations) {
        instantiate_operation(nested, nested_region);
      }
      scopes_.pop_back();
    }
  }

  Compiler& compiler_;
  const detail::GraphSyntax& syntax_;
  std::string owner_;
  Diagnostics& diagnostics_;
  std::size_t initial_diagnostics_ = 0;
  std::optional<Graph> graph_;
  std::optional<Graph::Edit> edit_;
  std::vector<
      std::unordered_map<std::string, std::optional<Value>>>
      scopes_;
  std::unordered_map<std::string, Type> expected_values_;
};

class RuntimeSyntax {
public:
  RuntimeSyntax(const Graph& graph, std::string_view name) : graph_(graph) {
    syntax_.name = std::string(name);
  }

  detail::GraphSyntax build() {
    for (const Value& argument : graph_.inputs()) {
      const std::string name = bind(argument, "arg");
      syntax_.arguments.push_back({name, value(argument.type()), {}});
    }
    for (const Value& output : graph_.outputs()) {
      syntax_.result_types.push_back(value(output.type()));
    }
    for (const Operation& operation : graph_.operations()) {
      syntax_.operations.push_back(convert(operation));
    }
    for (const Value& output : graph_.outputs()) {
      syntax_.returns.push_back({use(output), {}});
    }
    return std::move(syntax_);
  }

private:
  static bool equals_default(const ParameterValue& parameter,
                             const Module::Literal& literal) {
    return std::visit(
        [&](const auto& value) { return parameter == ParameterValue(value); },
        literal);
  }

  static std::size_t visible_parameters(
      std::span<const ParameterValue> parameters,
      std::span<const Module::ParameterDecl> schema) {
    std::size_t count = parameters.size();
    while (count != 0U && count <= schema.size() &&
           schema[count - 1U].default_value &&
           equals_default(parameters[count - 1U],
                          *schema[count - 1U].default_value)) {
      --count;
    }
    return count;
  }

  static detail::ValueSyntax value(const Type& type) {
    detail::ValueSyntax result;
    result.kind = detail::ValueSyntax::Kind::Reference;
    result.text = type.schema().symbol().qualified_name();
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
      std::ostringstream text;
      text.imbue(std::locale::classic());
      text << std::setprecision(std::numeric_limits<double>::max_digits10)
           << *parameter.as_f64();
      result.text = text.str();
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
          "the Graph contains a value outside canonical SSA order");
    }
    return found->second;
  }

  detail::GraphOperationSyntax convert(const Operation& operation) {
    detail::GraphOperationSyntax result;
    result.operation = operation.schema().symbol().qualified_name();
    for (const Value& output : operation.results()) {
      result.results.push_back(bind(output, "v"));
      result.result_types.push_back(value(output.type()));
    }
    for (const Value& operand : operation.operands()) {
      result.operands.push_back({use(operand), {}});
    }
    for (const Module::ParameterDecl& parameter : operation.schema().inputs()) {
      if (parameter.kind == Module::ParameterKind::Value ||
          parameter.kind == Module::ParameterKind::Region) {
        continue;
      }
      if (const auto property =
              detail::GraphAccess::property(operation, parameter.name)) {
        result.properties.push_back({parameter.name, value(*property), {}});
      }
    }
    for (const Region& region : operation.regions()) {
      detail::GraphRegionSyntax nested;
      nested.name = std::string(region.parameter());
      for (const Value& argument : region.arguments()) {
        const std::string name = bind(argument, "arg");
        nested.arguments.push_back({name, value(argument.type()), {}});
      }
      for (const Operation& child : region.operations()) {
        nested.operations.push_back(convert(child));
      }
      result.regions.push_back(std::move(nested));
    }
    return result;
  }

  const Graph& graph_;
  detail::GraphSyntax syntax_;
  std::vector<std::pair<Value, std::string>> names_;
  std::size_t next_value_ = 0;
};

}  // namespace

namespace detail {

std::optional<GraphSyntax> parse_graph_syntax(Lexer& lexer, Token& current,
                                              Diagnostics& diagnostics,
                                              std::string source) {
  return SyntaxParser(lexer, current, diagnostics, std::move(source)).parse();
}

std::string format_graph_syntax(const GraphSyntax& graph, std::size_t indent) {
  return SyntaxWriter(graph, indent).write();
}

std::optional<Graph> instantiate_graph(Compiler& compiler,
                                       const GraphSyntax& syntax,
                                       std::string_view owner,
                                       Diagnostics& diagnostics) {
  return Instantiator(compiler, syntax, owner, diagnostics).instantiate();
}

}  // namespace detail

std::string format(const Graph& graph, std::string_view name) {
  const auto identifier_character = [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  };
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_') ||
      !std::all_of(name.begin() + 1, name.end(), identifier_character)) {
    throw std::invalid_argument("a formatted Graph needs a valid name");
  }
  return detail::format_graph_syntax(RuntimeSyntax(graph, name).build(), 0U);
}

}  // namespace joggle
