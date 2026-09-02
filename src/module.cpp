#include "joggle/module.h"

#include "graph_member.h"
#include "module_internal.h"
#include "sha256.h"
#include "syntax_lexer.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace joggle {

namespace detail {

struct MethodDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> parameters;
  Module::ParameterKind result_kind = Module::ParameterKind::I64;
  bool result_list = false;
};

struct InterfaceDefinition {
  std::string name;
  Module::SymbolKind subject = Module::SymbolKind::Type;
  std::vector<MethodDefinition> methods;
  std::optional<SourceRange> source;
};

struct TypeDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> parameters;
  std::vector<std::string> interfaces;
  std::optional<SourceRange> source;
};

struct AttributeDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> parameters;
  std::vector<std::string> interfaces;
  std::optional<SourceRange> source;
};

struct OperationDefinition {
  std::string name;
  std::vector<Module::ParameterDecl> inputs;
  std::vector<Module::ParameterDecl> results;
  OperationTypeContract types;
  std::vector<std::string> interfaces;
  std::optional<SourceRange> source;
};

struct PassDefinition {
  std::string name;
  Module::PassDecl::Form form = Module::PassDecl::Form::External;
  std::vector<RuleDefinition> rules;
  std::vector<std::string> steps;
  std::optional<SourceRange> source;
};

}  // namespace detail

struct Module::Storage {
  std::string name;
  Version version;
  std::string digest;
  std::vector<Import> imports;
  std::vector<SourceRange> import_sources;
  std::vector<detail::InterfaceDefinition> interfaces;
  std::vector<detail::TypeDefinition> types;
  std::vector<detail::AttributeDefinition> attributes;
  std::vector<detail::OperationDefinition> operations;
  std::vector<detail::PassDefinition> passes;
  std::vector<detail::GraphSyntax> graphs;
};

namespace {

using Parameter = Module::ParameterDecl;
using ParameterKind = Module::ParameterKind;
using Literal = Module::Literal;

struct ParsedModule {
  using InterfaceDefinition = detail::InterfaceDefinition;
  using TypeDefinition = detail::TypeDefinition;
  using AttributeDefinition = detail::AttributeDefinition;
  using OperationDefinition = detail::OperationDefinition;
  using TypeExpression = detail::TypeExpression;
  using GenericDefinition = detail::GenericDefinition;
  using TermDefinition = detail::TermDefinition;
  using RuleDefinition = detail::RuleDefinition;
  using PassDefinition = detail::PassDefinition;

  std::string name;
  Version version;
  std::vector<Module::Import> imports;
  std::vector<SourceRange> import_sources;
  std::vector<InterfaceDefinition> interfaces;
  std::vector<TypeDefinition> types;
  std::vector<AttributeDefinition> attributes;
  std::vector<OperationDefinition> operations;
  std::vector<PassDefinition> passes;
  std::vector<detail::GraphSyntax> graphs;
};

using TokenKind = detail::TokenKind;
using Token = detail::Token;
using Lexer = detail::Lexer;

class Parser {
public:
  Parser(std::string_view text, std::string source)
      : lexer_(text), source_(std::move(source)) {
    advance();
  }

  std::optional<ParsedModule> parse(Diagnostics& output) {
    expect_name("joggle");
    const auto language = integer();
    if (language && *language != 1U) {
      error("unsupported Joggle language version");
    }
    expect(TokenKind::Semicolon, "';'");
    expect_name("module");
    auto module_name = name("a module name");
    expect(TokenKind::At, "'@'");
    auto module_version = exact_version();
    expect(TokenKind::LeftBrace, "'{'");
    if (module_name) {
      module_.name = std::move(*module_name);
    }
    if (module_version) {
      module_.version = *module_version;
    }

    while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
      if (is_name("import")) {
        const SourcePosition begin = current_.begin;
        advance();
        parse_import(begin);
      } else if (match_name("interface")) {
        parse_interface();
      } else if (match_name("type")) {
        parse_type();
      } else if (match_name("attr")) {
        parse_attribute();
      } else if (match_name("op")) {
        parse_operation();
      } else if (match_name("pass")) {
        parse_pass();
      } else if (is_name("graph")) {
        parse_graph();
      } else {
        error("expected import, interface, type, attr, op, pass, or graph");
      }
    }
    expect(TokenKind::RightBrace, "'}'");
    if (!is(TokenKind::End)) {
      error("unexpected input after module");
    }
    validate();

    if (!ok()) {
      for (Diagnostic& diagnostic : diagnostics_) {
        output.report(std::move(diagnostic));
      }
      return std::nullopt;
    }
    return std::move(module_);
  }

private:
  bool ok() const { return diagnostics_.empty(); }
  bool is(TokenKind kind) const { return current_.kind == kind; }
  bool is_name(std::string_view name) const {
    return is(TokenKind::Name) && current_.text == name;
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

  bool match_name(std::string_view expected) {
    if (!is(TokenKind::Name) || current_.text != expected) {
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

  void expect_name(std::string_view expected) {
    if (!match_name(expected)) {
      error("expected '" + std::string(expected) + "'");
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
    if (!second) {
      return std::nullopt;
    }
    return *first + "." + *second;
  }

  std::optional<std::size_t> pass_term(ParsedModule::RuleDefinition& rule) {
    ParsedModule::TermDefinition term;
    if (match(TokenKind::Dollar)) {
      term.kind = ParsedModule::TermDefinition::Kind::Variable;
      auto variable = name("a pass variable name");
      if (!variable) {
        return std::nullopt;
      }
      term.name = std::move(*variable);
    } else {
      term.kind = ParsedModule::TermDefinition::Kind::Operation;
      auto operation = reference("an operation name");
      if (!operation) {
        return std::nullopt;
      }
      term.name = std::move(*operation);
      expect(TokenKind::LeftParen, "'('");
      if (!match(TokenKind::RightParen)) {
        do {
          auto argument = pass_term(rule);
          if (argument) {
            term.arguments.push_back(*argument);
          }
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
    }
    rule.terms.push_back(std::move(term));
    return rule.terms.size() - 1U;
  }

  std::optional<std::uint64_t> integer() {
    if (!is(TokenKind::Integer)) {
      error("expected an integer");
      return std::nullopt;
    }
    std::uint64_t result = 0;
    const char* begin = current_.text.data();
    const char* end = begin + current_.text.size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      error("integer is outside the supported range");
      return std::nullopt;
    }
    advance();
    return result;
  }

  std::optional<Version> exact_version() {
    const auto components = version_components();
    if (!components || components->size() != 3U) {
      error("invalid semantic version");
      return std::nullopt;
    }
    return Version{static_cast<std::uint32_t>((*components)[0]),
                   static_cast<std::uint32_t>((*components)[1]),
                   static_cast<std::uint32_t>((*components)[2])};
  }

  std::optional<VersionRange> version_range() {
    const bool caret = match(TokenKind::Caret);
    const auto parsed = version_components();
    if (!parsed || parsed->empty() || parsed->size() > 3U) {
      error("invalid version range");
      return std::nullopt;
    }
    Version version{static_cast<std::uint32_t>((*parsed)[0]), 0, 0};
    if (parsed->size() > 1U) {
      version.minor = static_cast<std::uint32_t>((*parsed)[1]);
    }
    if (parsed->size() > 2U) {
      version.patch = static_cast<std::uint32_t>((*parsed)[2]);
    }
    if (caret && parsed->size() != 3U) {
      error("a caret range needs a complete semantic version");
      return std::nullopt;
    }
    const VersionRangeKind kind =
        caret                  ? VersionRangeKind::Caret
        : parsed->size() == 1U ? VersionRangeKind::Major
        : parsed->size() == 2U ? VersionRangeKind::Minor
                               : VersionRangeKind::Exact;
    return VersionRange{kind, version};
  }

  std::optional<std::vector<std::uint64_t>> version_components() {
    std::vector<std::uint64_t> result;
    const auto append = [&](std::string_view token) {
      std::size_t begin = 0;
      while (begin <= token.size()) {
        const std::size_t end = token.find('.', begin);
        const std::string_view part = token.substr(
            begin,
            end == std::string_view::npos ? token.size() - begin : end - begin);
        std::uint64_t value = 0;
        const auto parsed =
            std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != part.data() + part.size() || !fits_version(value)) {
          return false;
        }
        result.push_back(value);
        if (end == std::string_view::npos) {
          return true;
        }
        begin = end + 1U;
      }
      return true;
    };

    while (true) {
      if ((!is(TokenKind::Integer) && !is(TokenKind::Number)) ||
          !append(current_.text)) {
        return std::nullopt;
      }
      advance();
      if (!match(TokenKind::Dot)) {
        break;
      }
    }
    return result;
  }

  static bool fits_version(std::uint64_t value) {
    return value <= std::numeric_limits<std::uint32_t>::max();
  }

  std::optional<ParameterKind> scalar_parameter_kind() {
    if (match_name("i64")) {
      return ParameterKind::I64;
    }
    if (match_name("f64")) {
      return ParameterKind::F64;
    }
    if (match_name("bool")) {
      return ParameterKind::Boolean;
    }
    if (match_name("string")) {
      return ParameterKind::String;
    }
    if (match_name("type")) {
      return ParameterKind::Type;
    }
    if (match_name("attr")) {
      return ParameterKind::Attribute;
    }
    if (match_name("region")) {
      return ParameterKind::Region;
    }
    error("expected a parameter kind");
    return std::nullopt;
  }

  struct ParsedParameterKind {
    ParameterKind kind = ParameterKind::I64;
    bool list = false;
  };

  std::optional<ParsedParameterKind> parameter_kind() {
    const bool list = match_name("list");
    if (list) {
      expect(TokenKind::Less, "'<'");
    }
    const auto kind = scalar_parameter_kind();
    if (list) {
      expect(TokenKind::Greater, "'>'");
    }
    if (!kind) {
      return std::nullopt;
    }
    if (list &&
        (*kind == ParameterKind::Value || *kind == ParameterKind::Region)) {
      error("list elements cannot be value or region");
    }
    return ParsedParameterKind{*kind, list};
  }

  std::optional<Literal> literal(ParameterKind kind) {
    if (kind == ParameterKind::Boolean) {
      if (match_name("true")) {
        return Literal{true};
      }
      if (match_name("false")) {
        return Literal{false};
      }
      error("expected true or false");
      return std::nullopt;
    }
    if (kind == ParameterKind::String) {
      if (!is(TokenKind::String)) {
        error("expected a string literal");
        return std::nullopt;
      }
      std::string value = current_.text;
      advance();
      return Literal{std::move(value)};
    }
    if (kind != ParameterKind::I64 && kind != ParameterKind::F64) {
      error("this parameter kind cannot have a default value");
      return std::nullopt;
    }

    if (!is(TokenKind::Integer) && !is(TokenKind::Number)) {
      error("expected a numeric literal");
      return std::nullopt;
    }
    const std::string text = current_.text;
    advance();
    if (kind == ParameterKind::I64) {
      std::int64_t value = 0;
      const char* begin = text.data();
      const char* end = begin + text.size();
      const auto parsed = std::from_chars(begin, end, value);
      if (parsed.ec != std::errc{} || parsed.ptr != end) {
        error("integer default is outside int64 range");
        return std::nullopt;
      }
      return Literal{value};
    }

    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
      error("invalid floating-point default");
      return std::nullopt;
    }
    return Literal{value};
  }

  std::optional<Parameter> parameter(bool allow_graph_kinds,
                                     bool allow_variadic, bool allow_default) {
    auto parameter_name = name("a parameter name");
    expect(TokenKind::Colon, "':'");
    auto kind = parameter_kind();
    if (!parameter_name || !kind) {
      return std::nullopt;
    }
    if (!allow_graph_kinds && (kind->kind == ParameterKind::Value ||
                               kind->kind == ParameterKind::Region)) {
      error("value and region are only valid in operation signatures");
    }
    Parameter result{std::move(*parameter_name), kind->kind, kind->list, false,
                     std::nullopt};
    if (match(TokenKind::Ellipsis)) {
      if (!allow_variadic) {
        error("this parameter cannot be variadic");
      }
      result.variadic = true;
    }
    if (match(TokenKind::Equal)) {
      if (!allow_default || result.variadic) {
        error("this parameter cannot have a default value");
      }
      if (kind->list) {
        error("list parameters cannot have default values");
      } else {
        result.default_value = literal(kind->kind);
      }
    }
    return result;
  }

  std::vector<Parameter> parameters(bool allow_graph_kinds, bool allow_variadic,
                                    bool allow_default) {
    std::vector<Parameter> result;
    expect(TokenKind::LeftParen, "'('");
    if (match(TokenKind::RightParen)) {
      return result;
    }
    do {
      auto item = parameter(allow_graph_kinds, allow_variadic, allow_default);
      if (item) {
        result.push_back(std::move(*item));
      }
    } while (match(TokenKind::Comma));
    expect(TokenKind::RightParen, "')'");
    return result;
  }

  std::vector<ParsedModule::GenericDefinition> operation_generics() {
    std::vector<ParsedModule::GenericDefinition> result;
    if (!match(TokenKind::Less)) {
      return result;
    }
    if (!match(TokenKind::Greater)) {
      do {
        auto generic_name = name("a type variable name");
        expect(TokenKind::Colon, "':'");
        auto kind = parameter_kind();
        if (kind && (kind->kind == ParameterKind::Value ||
                     kind->kind == ParameterKind::Region)) {
          error("operation type variables cannot have kind value or region");
        }
        if (generic_name && kind) {
          result.push_back({std::move(*generic_name), kind->kind, kind->list});
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::Greater, "'>'");
    }
    return result;
  }

  const ParsedModule::GenericDefinition*
  operation_generic(std::span<const ParsedModule::GenericDefinition> generics,
                    std::string_view name) const {
    const auto found =
        std::find_if(generics.begin(), generics.end(),
                     [&](const auto& item) { return item.name == name; });
    return found == generics.end() ? nullptr : &*found;
  }

  ParsedModule::TypeExpression
  type_expression(std::span<const ParsedModule::GenericDefinition> generics) {
    ParsedModule::TypeExpression result;
    if (is(TokenKind::Integer) || is(TokenKind::Number)) {
      result.kind = ParsedModule::TypeExpression::Kind::Number;
      result.text = current_.text;
      advance();
      return result;
    }
    if (is(TokenKind::String)) {
      result.kind = ParsedModule::TypeExpression::Kind::String;
      result.text = current_.text;
      advance();
      return result;
    }
    if (is_name("true") || is_name("false")) {
      result.kind = ParsedModule::TypeExpression::Kind::Boolean;
      result.text = current_.text;
      advance();
      return result;
    }
    if (match(TokenKind::LeftBracket)) {
      result.kind = ParsedModule::TypeExpression::Kind::List;
      if (!match(TokenKind::RightBracket)) {
        do {
          result.arguments.push_back(type_expression(generics));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightBracket, "']'");
      }
      return result;
    }
    auto symbol = reference("a type expression");
    if (!symbol) {
      return result;
    }
    if (symbol->find('.') == std::string::npos &&
        operation_generic(generics, *symbol) != nullptr &&
        !is(TokenKind::Less)) {
      result.kind = ParsedModule::TypeExpression::Kind::Variable;
      result.text = std::move(*symbol);
      return result;
    }
    result.kind = ParsedModule::TypeExpression::Kind::Reference;
    result.text = std::move(*symbol);
    if (match(TokenKind::Less)) {
      if (!match(TokenKind::Greater)) {
        do {
          result.arguments.push_back(type_expression(generics));
        } while (match(TokenKind::Comma));
        expect(TokenKind::Greater, "'>'");
      }
    }
    return result;
  }

  std::pair<Parameter, std::optional<ParsedModule::TypeExpression>>
  operation_input(std::span<const ParsedModule::GenericDefinition> generics) {
    auto parameter_name = name("an operation parameter name");
    expect(TokenKind::Colon, "':'");
    Parameter parameter;
    if (parameter_name) {
      parameter.name = std::move(*parameter_name);
    }

    const auto* generic =
        is(TokenKind::Name) ? operation_generic(generics, current_.text)
                            : nullptr;
    const bool generic_property =
        generic != nullptr && generic->kind != ParameterKind::Type;
    const bool builtin = is_name("i64") || is_name("f64") || is_name("bool") ||
                         is_name("string") || is_name("type") ||
                         is_name("attr") || is_name("region") ||
                         is_name("list");
    std::optional<ParsedModule::TypeExpression> value_type;
    if (generic_property) {
      parameter.kind = generic->kind;
      parameter.list = generic->list;
      value_type = type_expression(generics);
    } else if (builtin) {
      auto kind = parameter_kind();
      if (kind) {
        parameter.kind = kind->kind;
        parameter.list = kind->list;
      }
    } else {
      parameter.kind = ParameterKind::Value;
      value_type = type_expression(generics);
    }

    if (match(TokenKind::Ellipsis)) {
      parameter.variadic = true;
    }
    if (match(TokenKind::Equal)) {
      if (parameter.kind == ParameterKind::Value ||
          parameter.kind == ParameterKind::Region || parameter.variadic ||
          parameter.list) {
        error("this operation parameter cannot have a default value");
      } else {
        parameter.default_value = literal(parameter.kind);
      }
    }
    return {std::move(parameter), std::move(value_type)};
  }

  void operation_inputs(
      std::span<const ParsedModule::GenericDefinition> generics,
      std::vector<Parameter>& parameters,
      std::vector<std::optional<ParsedModule::TypeExpression>>& types) {
    expect(TokenKind::LeftParen, "'('");
    if (!match(TokenKind::RightParen)) {
      do {
        auto [parameter, type] = operation_input(generics);
        parameters.push_back(std::move(parameter));
        types.push_back(std::move(type));
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }
  }

  std::vector<ParsedModule::TypeExpression>
  operation_results(std::span<const ParsedModule::GenericDefinition> generics) {
    std::vector<ParsedModule::TypeExpression> result;
    if (match(TokenKind::LeftParen)) {
      if (!match(TokenKind::RightParen)) {
        do {
          result.push_back(type_expression(generics));
        } while (match(TokenKind::Comma));
        expect(TokenKind::RightParen, "')'");
      }
    } else {
      result.push_back(type_expression(generics));
    }
    return result;
  }

  std::vector<std::string> interface_list() {
    std::vector<std::string> result;
    if (!match(TokenKind::Colon)) {
      return result;
    }
    do {
      auto interface = reference("an interface name");
      if (interface) {
        result.push_back(std::move(*interface));
      }
    } while (match(TokenKind::Comma));
    return result;
  }

  void parse_import(SourcePosition begin) {
    auto import_name = name("an imported module name");
    expect(TokenKind::At, "'@'");
    auto versions = version_range();
    std::optional<std::string> alias;
    if (match_name("as")) {
      alias = name("an import alias");
    }
    const SourcePosition end = current_.end;
    expect(TokenKind::Semicolon, "';'");
    if (import_name && versions) {
      std::string stored_alias =
          alias && *alias != *import_name ? std::move(*alias) : std::string{};
      module_.imports.push_back(
          {std::move(*import_name), *versions, std::move(stored_alias)});
      module_.import_sources.push_back(SourceRange{source_, begin, end});
    }
  }

  void parse_interface() {
    const SourcePosition begin = current_.begin;
    auto interface_name = name("an interface name");
    expect(TokenKind::Colon, "':'");
    std::optional<Module::SymbolKind> subject;
    if (match_name("type")) {
      subject = Module::SymbolKind::Type;
    } else if (match_name("attr")) {
      subject = Module::SymbolKind::Attribute;
    } else if (match_name("op")) {
      subject = Module::SymbolKind::Operation;
    } else {
      error("expected type, attr, or op after interface ':'");
    }

    ParsedModule::InterfaceDefinition definition;
    if (interface_name) {
      definition.name = std::move(*interface_name);
    }
    if (subject) {
      definition.subject = *subject;
    }
    if (match(TokenKind::Semicolon)) {
      if (!definition.name.empty()) {
        definition.source = SourceRange{source_, begin, current_.begin};
        module_.interfaces.push_back(std::move(definition));
      }
      return;
    }

    expect(TokenKind::LeftBrace, "'{'");
    while (!is(TokenKind::RightBrace) && ok()) {
      auto method_name = name("a method name");
      auto method_parameters = parameters(false, false, false);
      expect(TokenKind::Arrow, "'->'");
      auto result = parameter_kind();
      expect(TokenKind::Semicolon, "';'");
      if (result && (result->kind == ParameterKind::Value ||
                     result->kind == ParameterKind::Region)) {
        error("interface methods cannot return value or region");
      }
      if (method_name && result) {
        definition.methods.push_back({std::move(*method_name),
                                      std::move(method_parameters),
                                      result->kind, result->list});
      }
    }
    expect(TokenKind::RightBrace, "'}'");
    if (!definition.name.empty()) {
      definition.source = SourceRange{source_, begin, current_.begin};
      module_.interfaces.push_back(std::move(definition));
    }
  }

  void parse_type() {
    const SourcePosition begin = current_.begin;
    auto definition_name = name("a type name");
    auto definition_parameters = parameters(false, false, true);
    auto interfaces = interface_list();
    expect(TokenKind::Semicolon, "';'");
    if (definition_name) {
      module_.types.push_back(
          {std::move(*definition_name), std::move(definition_parameters),
           std::move(interfaces), SourceRange{source_, begin, current_.begin}});
    }
  }

  void parse_attribute() {
    const SourcePosition begin = current_.begin;
    auto definition_name = name("an attribute name");
    auto definition_parameters = parameters(false, false, true);
    auto interfaces = interface_list();
    expect(TokenKind::Semicolon, "';'");
    if (definition_name) {
      module_.attributes.push_back(
          {std::move(*definition_name), std::move(definition_parameters),
           std::move(interfaces), SourceRange{source_, begin, current_.begin}});
    }
  }

  void parse_operation() {
    const SourcePosition begin = current_.begin;
    auto definition_name = name("an operation name");
    auto generics = operation_generics();
    std::vector<Parameter> inputs;
    std::vector<std::optional<ParsedModule::TypeExpression>> input_types;
    operation_inputs(generics, inputs, input_types);
    std::vector<ParsedModule::TypeExpression> result_types;
    if (match(TokenKind::Arrow)) {
      result_types = operation_results(generics);
    }
    std::vector<Parameter> results;
    results.reserve(result_types.size());
    for (std::size_t index = 0; index < result_types.size(); ++index) {
      results.push_back({result_types.size() == 1U
                             ? "result"
                             : "result" + std::to_string(index),
                         ParameterKind::Value, false, false, std::nullopt});
    }

    ParsedModule::OperationDefinition definition;
    if (definition_name) {
      definition.name = std::move(*definition_name);
    }
    definition.inputs = std::move(inputs);
    definition.results = std::move(results);
    definition.types.generics = std::move(generics);
    definition.types.inputs = std::move(input_types);
    definition.types.results = std::move(result_types);
    definition.interfaces = interface_list();
    expect(TokenKind::Semicolon, "';'");
    if (!definition.name.empty()) {
      definition.source = SourceRange{source_, begin, current_.begin};
      module_.operations.push_back(std::move(definition));
    }
  }

  void parse_pass() {
    const SourcePosition begin = current_.begin;
    auto pass_name = name("a pass name");
    ParsedModule::PassDefinition definition;
    if (pass_name) {
      definition.name = std::move(*pass_name);
    }

    if (match(TokenKind::Semicolon)) {
      definition.form = Module::PassDecl::Form::External;
    } else if (match(TokenKind::Equal)) {
      definition.form = Module::PassDecl::Form::Sequence;
      do {
        auto step = reference("a pass name");
        if (step) {
          definition.steps.push_back(std::move(*step));
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::Semicolon, "';'");
    } else {
      definition.form = Module::PassDecl::Form::Rules;
      expect(TokenKind::LeftBrace, "'{'");
      while (!is(TokenKind::RightBrace) && ok()) {
        const SourcePosition rule_begin = current_.begin;
        ParsedModule::RuleDefinition rule;
        auto match_term = pass_term(rule);
        expect(TokenKind::FatArrow, "'=>'");
        auto replacement = pass_term(rule);
        if (match_term) {
          rule.match = *match_term;
        }
        if (replacement) {
          rule.replacement = *replacement;
        }
        expect(TokenKind::Semicolon, "';'");
        rule.source = SourceRange{source_, rule_begin, current_.begin};
        definition.rules.push_back(std::move(rule));
      }
      expect(TokenKind::RightBrace, "'}'");
      if (definition.rules.empty()) {
        error("a rule pass cannot be empty");
      }
    }
    if (!definition.name.empty()) {
      definition.source = SourceRange{source_, begin, current_.begin};
      module_.passes.push_back(std::move(definition));
    }
  }

  void parse_graph() {
    Diagnostics graph_diagnostics;
    auto graph = detail::parse_graph_syntax(lexer_, current_, graph_diagnostics,
                                            source_);
    for (const Diagnostic& diagnostic : graph_diagnostics.entries()) {
      diagnostics_.push_back(diagnostic);
    }
    if (graph) {
      module_.graphs.push_back(std::move(*graph));
    }
  }

  template <typename T>
  void check_unique(const std::vector<T>& values, std::string_view kind) {
    std::unordered_set<std::string> names;
    for (const T& value : values) {
      if (!names.insert(value.name).second) {
        error("duplicate " + std::string(kind) + " '" + value.name + "'");
      }
    }
  }

  static bool unique_parameter_names(const std::vector<Parameter>& values) {
    std::unordered_set<std::string> names;
    return std::all_of(values.begin(), values.end(), [&](const Parameter& p) {
      return names.insert(p.name).second;
    });
  }

  void validate_parameters(const std::vector<Parameter>& values,
                           std::string_view owner,
                           std::optional<SourceRange> source) {
    if (!unique_parameter_names(values)) {
      error("duplicate parameter in '" + std::string(owner) + "'", source);
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (values[index].variadic && index + 1U != values.size()) {
        error("variadic parameter must be last in '" + std::string(owner) + "'",
              source);
      }
    }
  }

  void validate_interface_uses(const std::vector<std::string>& interfaces,
                               Module::SymbolKind subject,
                               std::string_view owner,
                               std::optional<SourceRange> source) {
    std::unordered_set<std::string> names;
    for (const std::string& reference : interfaces) {
      if (!names.insert(reference).second) {
        error("duplicate interface '" + reference + "' on '" +
                  std::string(owner) + "'",
              source);
        continue;
      }
      const std::size_t dot = reference.find('.');
      if (dot != std::string::npos) {
        const std::string_view module_name(reference.data(), dot);
        const bool imported =
            std::any_of(module_.imports.begin(), module_.imports.end(),
                        [&](const Module::Import& import) {
                          return import.prefix() == module_name;
                        });
        if (module_name != module_.name && !imported) {
          error("interface '" + reference +
                    "' belongs to a module that is not imported",
                source);
        }
        continue;
      }
      const auto found = std::find_if(
          module_.interfaces.begin(), module_.interfaces.end(),
          [&](const auto& interface) { return interface.name == reference; });
      if (found == module_.interfaces.end()) {
        error("unknown interface '" + reference + "' on '" +
                  std::string(owner) + "'",
              source);
      } else if (found->subject != subject) {
        error("interface '" + reference + "' cannot be implemented by '" +
                  std::string(owner) + "'",
              source);
      }
    }
  }

  const ParsedModule::GenericDefinition*
  find_generic(const ParsedModule::OperationDefinition& operation,
               std::string_view name) const {
    return operation_generic(operation.types.generics, name);
  }

  void
  validate_type_expression(const ParsedModule::OperationDefinition& operation,
                           const ParsedModule::TypeExpression& expression,
                           ParameterKind expected, bool expected_list) {
    const auto report = [&](std::string message) {
      error(std::move(message), operation.source);
    };
    using Kind = ParsedModule::TypeExpression::Kind;
    if (expression.kind == Kind::Variable) {
      const auto* generic = find_generic(operation, expression.text);
      if (generic == nullptr || generic->kind != expected ||
          generic->list != expected_list) {
        report("type variable '" + expression.text +
               "' has the wrong kind in '" + operation.name + "'");
      }
      return;
    }
    if (expected_list) {
      if (expression.kind != Kind::List) {
        report("expected a list expression in operation '" + operation.name +
               "'");
        return;
      }
      for (const auto& element : expression.arguments) {
        validate_type_expression(operation, element, expected, false);
      }
      return;
    }
    if (expression.kind == Kind::List) {
      report("unexpected list expression in operation '" + operation.name +
             "'");
      return;
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      const bool matches = (expression.kind == Kind::Number &&
                            (expected == ParameterKind::I64 ||
                             expected == ParameterKind::F64)) ||
                           (expression.kind == Kind::Boolean &&
                            expected == ParameterKind::Boolean) ||
                           (expression.kind == Kind::String &&
                            expected == ParameterKind::String);
      if (!matches) {
        report("literal has the wrong kind in operation '" + operation.name +
               "'");
      }
      return;
    }
    if (!reference_is_visible(expression.text, "type", operation.source)) {
      return;
    }
    if (expected != ParameterKind::Type &&
        expected != ParameterKind::Attribute) {
      report("type expression reference has the wrong kind in operation '" +
             operation.name + "'");
      return;
    }
    const std::size_t dot = expression.text.find('.');
    const bool local = dot == std::string::npos ||
                       expression.text.substr(0, dot) == module_.name;
    if (!local) {
      return;
    }
    const std::string_view name =
        dot == std::string::npos
            ? std::string_view(expression.text)
            : std::string_view(expression.text).substr(dot + 1U);
    if (expected == ParameterKind::Type) {
      const auto declaration = std::find_if(
          module_.types.begin(), module_.types.end(),
          [&](const auto& candidate) { return candidate.name == name; });
      if (declaration == module_.types.end()) {
        report("unknown type '" + expression.text + "' in operation '" +
               operation.name + "'");
        return;
      }
      if (expression.arguments.size() > declaration->parameters.size()) {
        report("too many arguments for type '" + expression.text + "'");
        return;
      }
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        validate_type_expression(operation, expression.arguments[index],
                                 declaration->parameters[index].kind,
                                 declaration->parameters[index].list);
      }
      for (std::size_t index = expression.arguments.size();
           index < declaration->parameters.size(); ++index) {
        if (!declaration->parameters[index].default_value) {
          report("missing argument '" + declaration->parameters[index].name +
                 "' for type '" + expression.text + "'");
        }
      }
      return;
    }
    const auto declaration = std::find_if(
        module_.attributes.begin(), module_.attributes.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    if (declaration == module_.attributes.end()) {
      report("unknown attribute '" + expression.text + "' in operation '" +
             operation.name + "'");
    }
  }

  bool reference_is_visible(std::string_view reference, std::string_view kind,
                            std::optional<SourceRange> source = std::nullopt) {
    const std::size_t dot = reference.find('.');
    if (dot == std::string_view::npos) {
      return true;
    }
    const std::string_view module_name = reference.substr(0, dot);
    const bool imported =
        std::any_of(module_.imports.begin(), module_.imports.end(),
                    [&](const Module::Import& import) {
                      return import.prefix() == module_name;
                    });
    if (module_name != module_.name && !imported) {
      error(std::string(kind) + " reference '" + std::string(reference) +
                "' belongs to a module that is not imported",
            source);
      return false;
    }
    return true;
  }

  static bool same_term(const ParsedModule::RuleDefinition& rule,
                        std::size_t left, std::size_t right) {
    const auto& lhs = rule.terms[left];
    const auto& rhs = rule.terms[right];
    if (lhs.kind != rhs.kind || lhs.name != rhs.name ||
        lhs.arguments.size() != rhs.arguments.size()) {
      return false;
    }
    for (std::size_t index = 0; index < lhs.arguments.size(); ++index) {
      if (!same_term(rule, lhs.arguments[index], rhs.arguments[index])) {
        return false;
      }
    }
    return true;
  }

  static bool contains_term(const ParsedModule::RuleDefinition& rule,
                            std::size_t root, std::size_t candidate) {
    if (same_term(rule, root, candidate)) {
      return true;
    }
    const auto& term = rule.terms[root];
    return std::any_of(term.arguments.begin(), term.arguments.end(),
                       [&](std::size_t argument) {
                         return contains_term(rule, argument, candidate);
                       });
  }

  void validate_term(const ParsedModule::PassDefinition& pass,
                     const ParsedModule::RuleDefinition& rule,
                     std::size_t index) {
    const auto report = [&](std::string message) {
      error(std::move(message), rule.source ? rule.source : pass.source);
    };
    const auto& term = rule.terms[index];
    if (term.kind == ParsedModule::TermDefinition::Kind::Variable) {
      return;
    }
    if (!reference_is_visible(term.name, "operation", rule.source)) {
      return;
    }
    const std::size_t dot = term.name.find('.');
    const bool local =
        dot == std::string::npos || term.name.substr(0, dot) == module_.name;
    const std::string_view operation_name =
        dot == std::string::npos ? std::string_view(term.name)
                                 : std::string_view(term.name).substr(dot + 1U);
    if (local) {
      const auto operation =
          std::find_if(module_.operations.begin(), module_.operations.end(),
                       [&](const auto& candidate) {
                         return candidate.name == operation_name;
                       });
      if (operation == module_.operations.end()) {
        report("pass '" + pass.name + "' matches unknown operation '" +
               term.name + "'");
      } else {
        const auto count_kind = [](const auto& parameters, ParameterKind kind) {
          return static_cast<std::size_t>(
              std::count_if(parameters.begin(), parameters.end(),
                            [&](const Parameter& parameter) {
                              return parameter.kind == kind;
                            }));
        };
        if (count_kind(operation->inputs, ParameterKind::Value) !=
                term.arguments.size() ||
            count_kind(operation->results, ParameterKind::Value) != 1U) {
          report("pass '" + pass.name +
                 "' term arity does not match operation '" + term.name + "'");
        }
        if (count_kind(operation->inputs, ParameterKind::Region) != 0U) {
          report("pass '" + pass.name +
                 "' cannot contract an operation with regions");
        }
      }
    }
    for (std::size_t argument : term.arguments) {
      validate_term(pass, rule, argument);
    }
  }

  void validate_rule(const ParsedModule::PassDefinition& pass,
                     const ParsedModule::RuleDefinition& rule) {
    const auto report = [&](std::string message) {
      error(std::move(message), rule.source ? rule.source : pass.source);
    };
    if (rule.terms.empty() ||
        rule.terms[rule.match].kind !=
            ParsedModule::TermDefinition::Kind::Operation) {
      report("pass '" + pass.name + "' rule must match an operation term");
      return;
    }
    validate_term(pass, rule, rule.match);
    if (same_term(rule, rule.match, rule.replacement) ||
        !contains_term(rule, rule.match, rule.replacement)) {
      report("pass '" + pass.name +
             "' replacement must be a proper subterm of its match");
    }
  }

  void validate() {
    std::unordered_set<std::string_view> import_names;
    std::unordered_set<std::string_view> import_prefixes;
    for (std::size_t index = 0; index < module_.imports.size(); ++index) {
      const Module::Import& import = module_.imports[index];
      const SourceRange& source = module_.import_sources[index];
      if (!import_names.insert(import.name).second) {
        error("duplicate import '" + import.name + "'", source);
      }
      if (import.prefix() == module_.name) {
        error("import prefix '" + std::string(import.prefix()) +
                  "' conflicts with the module name",
              source);
      } else if (!import_prefixes.insert(import.prefix()).second) {
        error("duplicate import prefix '" + std::string(import.prefix()) + "'",
              source);
      }
    }
    check_unique(module_.interfaces, "interface");
    check_unique(module_.types, "type");
    check_unique(module_.attributes, "attribute");
    check_unique(module_.operations, "operation");
    check_unique(module_.passes, "pass");
    check_unique(module_.graphs, "graph");
    for (const auto& interface : module_.interfaces) {
      check_unique(interface.methods,
                   "method in interface '" + interface.name + "'");
      for (const auto& method : interface.methods) {
        validate_parameters(method.parameters,
                            interface.name + "." + method.name,
                            interface.source);
      }
    }
    for (const auto& type : module_.types) {
      validate_parameters(type.parameters, type.name, type.source);
      validate_interface_uses(type.interfaces, Module::SymbolKind::Type,
                              type.name, type.source);
    }
    for (const auto& attribute : module_.attributes) {
      validate_parameters(attribute.parameters, attribute.name,
                          attribute.source);
      validate_interface_uses(attribute.interfaces,
                              Module::SymbolKind::Attribute, attribute.name,
                              attribute.source);
    }
    for (const auto& operation : module_.operations) {
      validate_parameters(operation.inputs, operation.name, operation.source);
      validate_parameters(operation.results, operation.name, operation.source);
      std::unordered_set<std::string> generics;
      for (const auto& generic : operation.types.generics) {
        if (!generics.insert(generic.name).second) {
          error("duplicate type variable '" + generic.name + "' in '" +
                    operation.name + "'",
                operation.source);
        }
      }
      for (std::size_t index = 0; index < operation.inputs.size(); ++index) {
        if (operation.inputs[index].kind == ParameterKind::Value) {
          if (index >= operation.types.inputs.size() ||
              !operation.types.inputs[index]) {
            error("operation '" + operation.name + "' has an untyped operand",
                  operation.source);
          } else {
            validate_type_expression(operation, *operation.types.inputs[index],
                                     ParameterKind::Type, false);
          }
        } else if (index < operation.types.inputs.size() &&
                   operation.types.inputs[index]) {
          validate_type_expression(operation, *operation.types.inputs[index],
                                   operation.inputs[index].kind,
                                   operation.inputs[index].list);
        }
      }
      for (const auto& result : operation.types.results) {
        validate_type_expression(operation, result, ParameterKind::Type, false);
      }
      validate_interface_uses(operation.interfaces,
                              Module::SymbolKind::Operation, operation.name,
                              operation.source);
    }
    for (const auto& pass : module_.passes) {
      for (const auto& rule : pass.rules) {
        validate_rule(pass, rule);
      }
      for (const std::string& step : pass.steps) {
        if (!reference_is_visible(step, "pass", pass.source)) {
          continue;
        }
        const std::size_t dot = step.find('.');
        const bool local =
            dot == std::string::npos || step.substr(0, dot) == module_.name;
        const std::string_view local_name =
            dot == std::string::npos ? std::string_view(step)
                                     : std::string_view(step).substr(dot + 1U);
        if (local && std::none_of(module_.passes.begin(), module_.passes.end(),
                                  [&](const auto& candidate) {
                                    return candidate.name == local_name;
                                  })) {
          error("pass '" + pass.name + "' contains unknown pass '" + step + "'",
                pass.source);
        }
      }
    }
  }

  void error(std::string message) {
    diagnostics_.push_back({std::move(message),
                            SourceRange{source_, current_.begin, current_.end},
                            {}});
  }

  void error(std::string message, SourceRange source) {
    diagnostics_.push_back({std::move(message), std::move(source), {}});
  }

  void error(std::string message, std::optional<SourceRange> source) {
    if (source) {
      error(std::move(message), std::move(*source));
    } else {
      error(std::move(message));
    }
  }

  Lexer lexer_;
  std::string source_;
  Token current_;
  ParsedModule module_;
  std::vector<Diagnostic> diagnostics_;
};

std::string kind_name(Module::ParameterKind kind) {
  switch (kind) {
  case Module::ParameterKind::I64:
    return "i64";
  case Module::ParameterKind::F64:
    return "f64";
  case Module::ParameterKind::Boolean:
    return "bool";
  case Module::ParameterKind::String:
    return "string";
  case Module::ParameterKind::Type:
    return "type";
  case Module::ParameterKind::Attribute:
    return "attr";
  case Module::ParameterKind::Value:
    return "value";
  case Module::ParameterKind::Region:
    return "region";
  }
  return "invalid";
}

std::string symbol_kind_name(Module::SymbolKind kind) {
  switch (kind) {
  case Module::SymbolKind::Interface:
    return "interface";
  case Module::SymbolKind::Type:
    return "type";
  case Module::SymbolKind::Attribute:
    return "attr";
  case Module::SymbolKind::Operation:
    return "op";
  case Module::SymbolKind::Pass:
    return "pass";
  case Module::SymbolKind::Graph:
    return "graph";
  }
  return "invalid";
}

std::string subject_kind_name(Module::SymbolKind kind) {
  switch (kind) {
  case Module::SymbolKind::Type:
    return "type";
  case Module::SymbolKind::Attribute:
    return "attr";
  case Module::SymbolKind::Operation:
    return "op";
  case Module::SymbolKind::Interface:
  case Module::SymbolKind::Pass:
  case Module::SymbolKind::Graph:
    break;
  }
  return "invalid";
}

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

std::string literal_text(const Literal& literal) {
  return std::visit(
      [](const auto& value) -> std::string {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
          return value ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return escape(value);
        } else if constexpr (std::is_same_v<T, double>) {
          std::ostringstream output;
          output << std::setprecision(17) << value;
          return output.str();
        } else {
          return std::to_string(value);
        }
      },
      literal);
}

std::string parameter_text(const Parameter& parameter) {
  std::string type = kind_name(parameter.kind);
  if (parameter.list) {
    type = "list<" + type + ">";
  }
  std::string result = parameter.name + ": " + type;
  if (parameter.variadic) {
    result += "...";
  }
  if (parameter.default_value) {
    result += " = " + literal_text(*parameter.default_value);
  }
  return result;
}

std::string type_expression_text(const detail::TypeExpression& expression) {
  using Kind = detail::TypeExpression::Kind;
  if (expression.kind == Kind::String) {
    return escape(expression.text);
  }
  if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
      expression.kind == Kind::Variable) {
    return expression.text;
  }
  if (expression.kind == Kind::List) {
    std::string result = "[";
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      if (index != 0U) {
        result += ", ";
      }
      result += type_expression_text(expression.arguments[index]);
    }
    return result + "]";
  }
  std::string result = expression.text;
  if (!expression.arguments.empty()) {
    result += '<';
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      if (index != 0U) {
        result += ", ";
      }
      result += type_expression_text(expression.arguments[index]);
    }
    result += '>';
  }
  return result;
}

template <typename Definition>
std::optional<std::size_t>
find_definition(const std::vector<Definition>& values, std::string_view name) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const Definition& value) { return value.name == name; });
  if (found == values.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::distance(values.begin(), found));
}

Version upper_bound(VersionRange range) {
  Version upper = range.base;
  switch (range.kind) {
  case VersionRangeKind::Exact:
    return upper;
  case VersionRangeKind::Major:
    ++upper.major;
    upper.minor = 0;
    upper.patch = 0;
    return upper;
  case VersionRangeKind::Minor:
    ++upper.minor;
    upper.patch = 0;
    return upper;
  case VersionRangeKind::Caret:
    if (upper.major != 0U) {
      ++upper.major;
      upper.minor = 0;
      upper.patch = 0;
    } else if (upper.minor != 0U) {
      ++upper.minor;
      upper.patch = 0;
    } else {
      ++upper.patch;
    }
    return upper;
  }
  return upper;
}

}  // namespace

bool VersionRange::contains(Version candidate) const {
  if (kind == VersionRangeKind::Exact) {
    return candidate == base;
  }
  return candidate >= base && candidate < upper_bound(*this);
}

Module::Symbol::Symbol(std::string module_name, Version module_version,
                       std::string module_digest, SymbolKind kind,
                       std::string local_name)
    : module_name_(std::move(module_name)), module_version_(module_version),
      module_digest_(std::move(module_digest)), kind_(kind),
      local_name_(std::move(local_name)) {}

std::string Module::Symbol::qualified_name() const {
  return module_name_ + "." + local_name_;
}

std::string Module::Symbol::stable_name() const {
  return module_name_ + "@" + to_string(module_version_) + "#" +
         module_digest_ + "/" + symbol_kind_name(kind_) + "/" + local_name_;
}

Module::InterfaceDecl::MethodDecl::MethodDecl(
    std::shared_ptr<const Storage> storage, std::size_t interface_index,
    std::size_t method_index)
    : storage_(std::move(storage)), interface_index_(interface_index),
      method_index_(method_index) {}

std::string_view Module::InterfaceDecl::MethodDecl::name() const {
  return storage_->interfaces[interface_index_].methods[method_index_].name;
}

std::span<const Module::ParameterDecl>
Module::InterfaceDecl::MethodDecl::parameters() const {
  return storage_->interfaces[interface_index_]
      .methods[method_index_]
      .parameters;
}

Module::ParameterKind Module::InterfaceDecl::MethodDecl::result_kind() const {
  return storage_->interfaces[interface_index_]
      .methods[method_index_]
      .result_kind;
}

bool Module::InterfaceDecl::MethodDecl::result_is_list() const {
  return storage_->interfaces[interface_index_]
      .methods[method_index_]
      .result_list;
}

Module::InterfaceDecl Module::InterfaceDecl::MethodDecl::owner() const {
  return InterfaceDecl(storage_, interface_index_);
}

std::string Module::InterfaceDecl::MethodDecl::qualified_name() const {
  return storage_->name + "." + storage_->interfaces[interface_index_].name +
         "." + std::string(name());
}

std::string Module::InterfaceDecl::MethodDecl::stable_name() const {
  return storage_->name + "@" + to_string(storage_->version) + "#" +
         storage_->digest + "/interface/" +
         storage_->interfaces[interface_index_].name + "/method/" +
         std::string(name());
}

bool Module::InterfaceDecl::MethodDecl::operator==(
    const MethodDecl& other) const {
  return stable_name() == other.stable_name();
}

Module::InterfaceDecl::InterfaceDecl(std::shared_ptr<const Storage> storage,
                                     std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::InterfaceDecl::name() const {
  return storage_->interfaces[index_].name;
}

Module::SymbolKind Module::InterfaceDecl::subject() const {
  return storage_->interfaces[index_].subject;
}

std::optional<Module::InterfaceDecl::MethodDecl>
Module::InterfaceDecl::method(std::string_view name) const {
  const auto index =
      find_definition(storage_->interfaces[index_].methods, name);
  return index ? std::optional<MethodDecl>{MethodDecl(storage_, index_, *index)}
               : std::nullopt;
}

std::vector<Module::InterfaceDecl::MethodDecl>
Module::InterfaceDecl::methods() const {
  std::vector<MethodDecl> result;
  const auto& methods = storage_->interfaces[index_].methods;
  result.reserve(methods.size());
  for (std::size_t method_index = 0; method_index < methods.size();
       ++method_index) {
    result.push_back(MethodDecl(storage_, index_, method_index));
  }
  return result;
}

Module::Symbol Module::InterfaceDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->digest,
          SymbolKind::Interface, storage_->interfaces[index_].name};
}

bool Module::InterfaceDecl::operator==(const InterfaceDecl& other) const {
  return symbol() == other.symbol();
}

Module::TypeDecl::TypeDecl(std::shared_ptr<const Storage> storage,
                           std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::TypeDecl::name() const {
  return storage_->types[index_].name;
}

std::span<const Parameter> Module::TypeDecl::parameters() const {
  return storage_->types[index_].parameters;
}

std::span<const std::string> Module::TypeDecl::interfaces() const {
  return storage_->types[index_].interfaces;
}

Module::Symbol Module::TypeDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->digest, SymbolKind::Type,
          storage_->types[index_].name};
}

bool Module::TypeDecl::operator==(const TypeDecl& other) const {
  return symbol() == other.symbol();
}

Module::AttributeDecl::AttributeDecl(std::shared_ptr<const Storage> storage,
                                     std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::AttributeDecl::name() const {
  return storage_->attributes[index_].name;
}

std::span<const Parameter> Module::AttributeDecl::parameters() const {
  return storage_->attributes[index_].parameters;
}

std::span<const std::string> Module::AttributeDecl::interfaces() const {
  return storage_->attributes[index_].interfaces;
}

Module::Symbol Module::AttributeDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->digest,
          SymbolKind::Attribute, storage_->attributes[index_].name};
}

bool Module::AttributeDecl::operator==(const AttributeDecl& other) const {
  return symbol() == other.symbol();
}

Module::OperationDecl::OperationDecl(std::shared_ptr<const Storage> storage,
                                     std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::OperationDecl::name() const {
  return storage_->operations[index_].name;
}

std::span<const Parameter> Module::OperationDecl::inputs() const {
  return storage_->operations[index_].inputs;
}

std::span<const Parameter> Module::OperationDecl::results() const {
  return storage_->operations[index_].results;
}

std::span<const std::string> Module::OperationDecl::interfaces() const {
  return storage_->operations[index_].interfaces;
}

Module::Symbol Module::OperationDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->digest,
          SymbolKind::Operation, storage_->operations[index_].name};
}

bool Module::OperationDecl::operator==(const OperationDecl& other) const {
  return symbol() == other.symbol();
}

const detail::OperationTypeContract&
detail::OperationTypeAccess::get(const Module::OperationDecl& operation) {
  return operation.storage_->operations[operation.index_].types;
}

Module::PassDecl::PassDecl(std::shared_ptr<const Storage> storage,
                           std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::PassDecl::name() const {
  return storage_->passes[index_].name;
}

Module::PassDecl::Form Module::PassDecl::form() const {
  return storage_->passes[index_].form;
}

std::span<const std::string> Module::PassDecl::steps() const {
  return storage_->passes[index_].steps;
}

Module::Symbol Module::PassDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->digest, SymbolKind::Pass,
          storage_->passes[index_].name};
}

bool Module::PassDecl::operator==(const PassDecl& other) const {
  return symbol() == other.symbol();
}

Module::Module(std::shared_ptr<const Storage> storage)
    : storage_(std::move(storage)) {}

std::string_view Module::name() const { return storage_->name; }

Version Module::version() const { return storage_->version; }

std::string_view Module::digest() const { return storage_->digest; }

std::span<const Module::Import> Module::imports() const {
  return storage_->imports;
}

std::optional<Module::InterfaceDecl>
Module::interface(std::string_view name) const {
  const auto index = find_definition(storage_->interfaces, name);
  return index ? std::optional<InterfaceDecl>{InterfaceDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::TypeDecl> Module::type(std::string_view name) const {
  const auto index = find_definition(storage_->types, name);
  return index ? std::optional<TypeDecl>{TypeDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::AttributeDecl>
Module::attribute(std::string_view name) const {
  const auto index = find_definition(storage_->attributes, name);
  return index ? std::optional<AttributeDecl>{AttributeDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::OperationDecl>
Module::operation(std::string_view name) const {
  const auto index = find_definition(storage_->operations, name);
  return index ? std::optional<OperationDecl>{OperationDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::PassDecl> Module::pass(std::string_view name) const {
  const auto index = find_definition(storage_->passes, name);
  return index ? std::optional<PassDecl>{PassDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::Symbol> Module::symbol(SymbolKind kind,
                                             std::string_view name) const {
  const bool exists =
      (kind == SymbolKind::Interface && interface(name).has_value()) ||
      (kind == SymbolKind::Type && type(name).has_value()) ||
      (kind == SymbolKind::Attribute && attribute(name).has_value()) ||
      (kind == SymbolKind::Operation && operation(name).has_value()) ||
      (kind == SymbolKind::Pass && pass(name).has_value()) ||
      (kind == SymbolKind::Graph &&
       find_definition(storage_->graphs, name).has_value());
  if (!exists) {
    return std::nullopt;
  }
  return Symbol(std::string(storage_->name), storage_->version,
                storage_->digest, kind, std::string(name));
}

std::vector<Module::Symbol> Module::members() const {
  std::vector<Symbol> result;
  result.reserve(storage_->interfaces.size() + storage_->types.size() +
                 storage_->attributes.size() + storage_->operations.size() +
                 storage_->passes.size() + storage_->graphs.size());
  const auto append = [&](SymbolKind kind, const auto& definitions) {
    for (const auto& definition : definitions) {
      result.push_back(Symbol(std::string(storage_->name), storage_->version,
                              storage_->digest, kind,
                              std::string(definition.name)));
    }
  };
  append(SymbolKind::Interface, storage_->interfaces);
  append(SymbolKind::Type, storage_->types);
  append(SymbolKind::Attribute, storage_->attributes);
  append(SymbolKind::Operation, storage_->operations);
  append(SymbolKind::Pass, storage_->passes);
  append(SymbolKind::Graph, storage_->graphs);
  return result;
}

std::vector<Module::InterfaceDecl> Module::interfaces() const {
  std::vector<InterfaceDecl> result;
  result.reserve(storage_->interfaces.size());
  for (std::size_t index = 0; index < storage_->interfaces.size(); ++index) {
    result.push_back(InterfaceDecl(storage_, index));
  }
  return result;
}

std::vector<Module::TypeDecl> Module::types() const {
  std::vector<TypeDecl> result;
  result.reserve(storage_->types.size());
  for (std::size_t index = 0; index < storage_->types.size(); ++index) {
    result.push_back(TypeDecl(storage_, index));
  }
  return result;
}

std::vector<Module::AttributeDecl> Module::attributes() const {
  std::vector<AttributeDecl> result;
  result.reserve(storage_->attributes.size());
  for (std::size_t index = 0; index < storage_->attributes.size(); ++index) {
    result.push_back(AttributeDecl(storage_, index));
  }
  return result;
}

std::vector<Module::OperationDecl> Module::operations() const {
  std::vector<OperationDecl> result;
  result.reserve(storage_->operations.size());
  for (std::size_t index = 0; index < storage_->operations.size(); ++index) {
    result.push_back(OperationDecl(storage_, index));
  }
  return result;
}

std::vector<Module::PassDecl> Module::passes() const {
  std::vector<PassDecl> result;
  result.reserve(storage_->passes.size());
  for (std::size_t index = 0; index < storage_->passes.size(); ++index) {
    result.push_back(PassDecl(storage_, index));
  }
  return result;
}

bool Module::operator==(const Module& other) const {
  return name() == other.name() && version() == other.version() &&
         digest() == other.digest();
}

std::shared_ptr<const detail::GraphSyntax>
detail::ModuleAccess::graph(const Module& module, std::string_view name) {
  const auto index = find_definition(module.storage_->graphs, name);
  if (!index) {
    return nullptr;
  }
  return std::shared_ptr<const detail::GraphSyntax>(
      module.storage_, &module.storage_->graphs[*index]);
}

std::optional<SourceRange>
detail::ModuleAccess::import_source(const Module& module, std::size_t index) {
  if (index >= module.storage_->import_sources.size()) {
    return std::nullopt;
  }
  return module.storage_->import_sources[index];
}

std::optional<SourceRange> detail::ModuleAccess::declaration_source(
    const Module& module, Module::SymbolKind kind, std::string_view name) {
  const auto find_source = [&](const auto& definitions) {
    const auto index = find_definition(definitions, name);
    return index ? definitions[*index].source : std::optional<SourceRange>{};
  };
  switch (kind) {
  case Module::SymbolKind::Interface:
    return find_source(module.storage_->interfaces);
  case Module::SymbolKind::Type:
    return find_source(module.storage_->types);
  case Module::SymbolKind::Attribute:
    return find_source(module.storage_->attributes);
  case Module::SymbolKind::Operation:
    return find_source(module.storage_->operations);
  case Module::SymbolKind::Pass:
    return find_source(module.storage_->passes);
  case Module::SymbolKind::Graph:
    if (const auto index = find_definition(module.storage_->graphs, name)) {
      const auto& graph = module.storage_->graphs[*index];
      return SourceRange{graph.source, graph.range.begin, graph.range.end};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

std::span<const detail::RuleDefinition>
detail::ModuleAccess::rules(const Module& module,
                            const Module::PassDecl& pass) {
  if (pass.storage_.get() != module.storage_.get() ||
      pass.index_ >= module.storage_->passes.size()) {
    return {};
  }
  return module.storage_->passes[pass.index_].rules;
}

std::optional<Module> parse_module(std::string_view text,
                                   Diagnostics& diagnostics,
                                   std::string source) {
  auto parsed = Parser(text, std::move(source)).parse(diagnostics);
  if (!parsed) {
    return std::nullopt;
  }
  auto storage = std::make_shared<Module::Storage>();
  storage->name = std::move(parsed->name);
  storage->version = parsed->version;
  storage->imports = std::move(parsed->imports);
  storage->import_sources = std::move(parsed->import_sources);
  storage->interfaces = std::move(parsed->interfaces);
  storage->types = std::move(parsed->types);
  storage->attributes = std::move(parsed->attributes);
  storage->operations = std::move(parsed->operations);
  storage->passes = std::move(parsed->passes);
  storage->graphs = std::move(parsed->graphs);
  Module module(storage);
  storage->digest = detail::sha256(format(module));
  return module;
}

std::string to_string(Version version) {
  return std::to_string(version.major) + "." + std::to_string(version.minor) +
         "." + std::to_string(version.patch);
}

std::string to_string(VersionRange range) {
  std::string result;
  if (range.kind == VersionRangeKind::Caret) {
    result += '^';
  }
  result += std::to_string(range.base.major);
  if (range.kind == VersionRangeKind::Major) {
    return result;
  }
  result += "." + std::to_string(range.base.minor);
  if (range.kind == VersionRangeKind::Minor) {
    return result;
  }
  return result + "." + std::to_string(range.base.patch);
}

std::string format(const Module& module) {
  std::ostringstream output;
  output << "joggle 1;\n\nmodule " << module.name() << '@'
         << to_string(module.version()) << " {\n";
  for (const Module::Import& import : module.imports()) {
    output << "  import " << import.name << '@' << to_string(import.version);
    if (!import.alias.empty()) {
      output << " as " << import.alias;
    }
    output << ";\n";
  }

  const auto write_parameters = [&](std::span<const Parameter> parameters) {
    output << '(';
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (index != 0U) {
        output << ", ";
      }
      output << parameter_text(parameters[index]);
    }
    output << ')';
  };
  const auto interfaces_text = [&](std::span<const std::string> interfaces) {
    std::string text;
    if (interfaces.empty()) {
      return text;
    }
    text = " : ";
    for (std::size_t index = 0; index < interfaces.size(); ++index) {
      if (index != 0U) {
        text += ", ";
      }
      text += interfaces[index];
    }
    return text;
  };
  bool wrote_group = !module.imports().empty();
  const auto begin_group = [&](bool present) {
    if (!present) {
      return;
    }
    if (wrote_group) {
      output << '\n';
    }
    wrote_group = true;
  };
  const auto write_term = [&](const auto& self,
                              const detail::RuleDefinition& rule,
                              std::size_t index) -> void {
    const auto& term = rule.terms[index];
    if (term.kind == detail::TermDefinition::Kind::Variable) {
      output << '$' << term.name;
      return;
    }
    output << term.name << '(';
    for (std::size_t argument = 0; argument < term.arguments.size();
         ++argument) {
      if (argument != 0U) {
        output << ", ";
      }
      self(self, rule, term.arguments[argument]);
    }
    output << ')';
  };

  begin_group(!module.storage_->interfaces.empty());
  for (const auto& interface : module.storage_->interfaces) {
    output << "  interface " << interface.name << ": "
           << subject_kind_name(interface.subject);
    if (interface.methods.empty()) {
      output << ";\n";
      continue;
    }
    output << " {\n";
    for (const auto& method : interface.methods) {
      output << "    " << method.name;
      write_parameters(method.parameters);
      std::string result = kind_name(method.result_kind);
      if (method.result_list) {
        result = "list<" + result + ">";
      }
      output << " -> " << result << ";\n";
    }
    output << "  }\n";
  }
  begin_group(!module.storage_->types.empty() ||
              !module.storage_->attributes.empty());
  for (const auto& type : module.storage_->types) {
    const std::string head = "  type " + type.name;
    std::string flat = head + '(';
    for (std::size_t index = 0; index < type.parameters.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += parameter_text(type.parameters[index]);
    }
    flat += ')' + interfaces_text(type.interfaces) + ";\n";
    if (flat.size() <= 89U || type.parameters.empty()) {
      output << flat;
      continue;
    }
    output << head << "(\n";
    for (std::size_t index = 0; index < type.parameters.size(); ++index) {
      output << "    " << parameter_text(type.parameters[index]);
      if (index + 1U != type.parameters.size()) {
        output << ',';
      }
      output << '\n';
    }
    output << "  )" << interfaces_text(type.interfaces) << ";\n";
  }
  for (const auto& attribute : module.storage_->attributes) {
    output << "  attr " << attribute.name;
    write_parameters(attribute.parameters);
    output << interfaces_text(attribute.interfaces);
    output << ";\n";
  }
  begin_group(!module.storage_->operations.empty());
  for (const auto& operation : module.storage_->operations) {
    std::string head = "  op " + operation.name;
    if (!operation.types.generics.empty()) {
      head += '<';
      for (std::size_t index = 0; index < operation.types.generics.size();
           ++index) {
        if (index != 0U) {
          head += ", ";
        }
        const auto& generic = operation.types.generics[index];
        head += generic.name + ": ";
        if (generic.list) {
          head += "list<";
        }
        head += kind_name(generic.kind);
        if (generic.list) {
          head += '>';
        }
      }
      head += '>';
    }
    std::vector<std::string> inputs;
    inputs.reserve(operation.inputs.size());
    for (std::size_t index = 0; index < operation.inputs.size(); ++index) {
      const auto& input = operation.inputs[index];
      if (operation.types.inputs[index]) {
        std::string text = input.name + ": " +
                           type_expression_text(*operation.types.inputs[index]);
        if (input.variadic) {
          text += "...";
        }
        if (input.default_value) {
          text += " = " + literal_text(*input.default_value);
        }
        inputs.push_back(std::move(text));
      } else {
        inputs.push_back(parameter_text(input));
      }
    }
    std::string tail = ")";
    if (!operation.types.results.empty()) {
      tail += " -> ";
      if (operation.types.results.size() > 1U) {
        tail += '(';
      }
      for (std::size_t index = 0; index < operation.types.results.size();
           ++index) {
        if (index != 0U) {
          tail += ", ";
        }
        tail += type_expression_text(operation.types.results[index]);
      }
      if (operation.types.results.size() > 1U) {
        tail += ')';
      }
    }
    if (!operation.interfaces.empty()) {
      tail += " : ";
      for (std::size_t index = 0; index < operation.interfaces.size();
           ++index) {
        if (index != 0U) {
          tail += ", ";
        }
        tail += operation.interfaces[index];
      }
    }
    std::string flat = head + '(';
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += inputs[index];
    }
    flat += tail + ";\n";
    if (flat.size() <= 89U) {
      output << flat;
      continue;
    }
    output << head << "(\n";
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      output << "    " << inputs[index];
      if (index + 1U != inputs.size()) {
        output << ',';
      }
      output << '\n';
    }
    output << "  " << tail << ";\n";
  }
  begin_group(!module.storage_->passes.empty());
  for (const auto& pass : module.storage_->passes) {
    output << "  pass " << pass.name;
    if (pass.form == Module::PassDecl::Form::External) {
      output << ";\n";
      continue;
    }
    if (pass.form == Module::PassDecl::Form::Sequence) {
      output << " = ";
      for (std::size_t index = 0; index < pass.steps.size(); ++index) {
        if (index != 0U) {
          output << ", ";
        }
        output << pass.steps[index];
      }
      output << ";\n";
      continue;
    }
    output << " {\n";
    for (const auto& rule : pass.rules) {
      output << "    ";
      write_term(write_term, rule, rule.match);
      output << " => ";
      write_term(write_term, rule, rule.replacement);
      output << ";\n";
    }
    output << "  }\n";
  }
  begin_group(!module.storage_->graphs.empty());
  for (const auto& graph : module.storage_->graphs) {
    output << detail::format_graph_syntax(graph, 1U);
  }
  output << "}\n";
  return output.str();
}

}  // namespace joggle
