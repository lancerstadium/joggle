#include "joggle/module.h"
#include "joggle/ir.h"

#include "domain.h"
#include "expression_syntax.h"
#include "prelude.h"
#include "function_body.h"
#include "module_internal.h"
#include "module_storage.h"
#include "joggle/digest.h"
#include "syntax_lexer.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace joggle {

namespace {

using Parameter = Module::ParameterDecl;
using ValueKind = detail::ValueKind;

struct ParsedModule {
  using TypeDefinition = detail::TypeDefinition;
  using FunctionDefinition = detail::FunctionDefinition;
  using TypeExpression = detail::TypeExpression;
  using GenericDefinition = detail::GenericDefinition;

  std::string name;
  Version version;
  std::vector<Module::Import> imports;
  std::vector<SourceRange> import_sources;
  std::vector<TypeDefinition> types;
  std::vector<FunctionDefinition> functions;
};

using TokenKind = detail::TokenKind;
using Token = detail::Token;
using Lexer = detail::Lexer;

const Module::Expression*
body_expression(const std::optional<detail::FunctionBody>& body) {
  if (!body || body->blocks.size() != 1U || body->blocks.front().terminator ||
      body->blocks.front().statements.size() != 1U ||
      body->blocks.front().statements.front().kind !=
          detail::StatementSyntax::Kind::Return ||
      body->blocks.front().statements.front().values.size() != 1U) {
    return nullptr;
  }
  return &body->blocks.front().statements.front().values.front().value;
}

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
      } else if (match_name("type")) {
        parse_type();
      } else if (is_name("fn")) {
        parse_function();
      } else {
        error("expected import, type, or fn");
      }
    }
    expect(TokenKind::RightBrace, "'}'");
    if (!is(TokenKind::End)) {
      error("unexpected input after module");
    }
    validate();

    if (!ok()) {
      for (const Diagnostic& diagnostic : diagnostics_.entries()) {
        output.report(diagnostic);
      }
      return std::nullopt;
    }
    return std::move(module_);
  }

private:
  bool ok() const { return diagnostics_.ok(); }
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

  std::optional<std::string> operator_symbol() {
    const auto is_symbol = [this] {
      return is(TokenKind::Equal) || is(TokenKind::Less) ||
             is(TokenKind::Greater) || is(TokenKind::Plus) ||
             is(TokenKind::Minus) || is(TokenKind::Star) ||
             is(TokenKind::Slash) || is(TokenKind::Caret) ||
             is(TokenKind::Pipe) || is(TokenKind::Operator);
    };
    if (is_symbol()) {
      std::string result;
      do {
        result += current_.text;
        const SourcePosition end = current_.end;
        advance();
        if (current_.begin != end) {
          break;
        }
      } while (is_symbol());
      return result;
    }
    error("expected an operator symbol");
    return std::nullopt;
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

  std::optional<ValueKind> scalar_parameter_kind() {
    if (match_name("int")) {
      return ValueKind::Integer;
    }
    if (match_name("real")) {
      return ValueKind::Real;
    }
    if (match_name("bool")) {
      return ValueKind::Boolean;
    }
    if (match_name("string")) {
      return ValueKind::String;
    }
    if (match_name("type")) {
      return ValueKind::Type;
    }
    if (match_name("function")) {
      return ValueKind::Function;
    }
    if (match_name("bytes")) {
      return ValueKind::Bytes;
    }
    error("expected a parameter domain");
    return std::nullopt;
  }

  std::optional<detail::Domain> parameter_domain() {
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
    return detail::Domain{*kind, list};
  }

  std::optional<ParsedModule::TypeExpression> literal(ValueKind kind) {
    using Expression = ParsedModule::TypeExpression;
    if (kind == ValueKind::Boolean) {
      if (match_name("true")) {
        return Expression{Expression::Kind::Boolean, "true", {}};
      }
      if (match_name("false")) {
        return Expression{Expression::Kind::Boolean, "false", {}};
      }
      error("expected true or false");
      return std::nullopt;
    }
    if (kind == ValueKind::String) {
      if (!is(TokenKind::String)) {
        error("expected a string literal");
        return std::nullopt;
      }
      std::string value = current_.text;
      advance();
      return Expression{Expression::Kind::String, std::move(value), {}};
    }
    if (kind != ValueKind::Integer && kind != ValueKind::Real) {
      error("this parameter domain cannot have a default value");
      return std::nullopt;
    }

    const bool negative = match(TokenKind::Minus);
    if (!is(TokenKind::Integer) && !is(TokenKind::Number)) {
      error("expected a numeric literal");
      return std::nullopt;
    }
    const std::string text = negative ? "-" + current_.text : current_.text;
    advance();
    if (kind == ValueKind::Integer) {
      std::int64_t value = 0;
      const char* begin = text.data();
      const char* end = begin + text.size();
      const auto parsed = std::from_chars(begin, end, value);
      if (parsed.ec != std::errc{} || parsed.ptr != end) {
        error("integer default is outside int64 range");
        return std::nullopt;
      }
      return Expression{Expression::Kind::Number, std::to_string(value), {}};
    }

    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
      error("invalid floating-point default");
      return std::nullopt;
    }
    const auto canonical = detail::canonical_real(value);
    if (!canonical) {
      error("floating-point default cannot be formatted canonically");
      return std::nullopt;
    }
    return Expression{Expression::Kind::Number, *canonical, {}};
  }

  std::optional<Parameter> parameter(bool allow_variadic, bool allow_default) {
    auto parameter_name = name("a parameter name");
    expect(TokenKind::Colon, "':'");
    auto domain = parameter_domain();
    if (!parameter_name || !domain) {
      return std::nullopt;
    }
    Parameter result{std::move(*parameter_name),
                     detail::domain_expression(domain->element, domain->list),
                     false, std::nullopt};
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
      if (domain->list) {
        error("list parameters cannot have default values");
      } else {
        result.default_value = literal(domain->element);
      }
    }
    return result;
  }

  std::vector<Parameter> parameters(bool allow_variadic, bool allow_default) {
    std::vector<Parameter> result;
    expect(TokenKind::LeftParen, "'('");
    if (match(TokenKind::RightParen)) {
      return result;
    }
    do {
      auto item = parameter(allow_variadic, allow_default);
      if (item) {
        result.push_back(std::move(*item));
      }
    } while (match(TokenKind::Comma));
    expect(TokenKind::RightParen, "')'");
    return result;
  }

  std::vector<ParsedModule::GenericDefinition> function_generics() {
    std::vector<ParsedModule::GenericDefinition> result;
    if (!match(TokenKind::Less)) {
      return result;
    }
    if (!match(TokenKind::Greater)) {
      do {
        auto generic_name = name("a type variable name");
        std::optional<detail::Domain> domain =
            detail::Domain{ValueKind::Type, false};
        if (match(TokenKind::Colon)) {
          domain = parameter_domain();
        }
        if (generic_name && domain) {
          result.push_back(
              {std::move(*generic_name),
               detail::domain_expression(domain->element, domain->list)});
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::Greater, "'>'");
    }
    return result;
  }

  const ParsedModule::GenericDefinition*
  find_generic(std::span<const ParsedModule::GenericDefinition> generics,
               std::string_view name) const {
    const auto found =
        std::find_if(generics.begin(), generics.end(),
                     [&](const auto& item) { return item.name == name; });
    return found == generics.end() ? nullptr : &*found;
  }

  bool
  is_ir_port(const ParsedModule::TypeExpression& annotation,
             std::span<const ParsedModule::GenericDefinition> generics) const {
    if (detail::kernel_domain(annotation)) {
      return false;
    }
    if (annotation.kind == ParsedModule::TypeExpression::Kind::Variable) {
      const auto* generic = find_generic(generics, annotation.text);
      const auto domain = generic == nullptr
                              ? std::optional<detail::Domain>{}
                              : detail::kernel_domain(generic->domain);
      if (domain && domain->element != ValueKind::Type) {
        return false;
      }
    }
    return true;
  }

  ParsedModule::TypeExpression
  type_expression(std::span<const ParsedModule::GenericDefinition> generics,
                  int minimum_precedence = 0) {
    return detail::parse_expression(lexer_, current_, diagnostics_, source_,
                                    generics, minimum_precedence);
  }

  std::vector<ParsedModule::TypeExpression>
  function_results(std::span<const ParsedModule::GenericDefinition> generics) {
    std::vector<ParsedModule::TypeExpression> result;
    const auto starts_function_type = [&] {
      if (!is(TokenKind::LeftParen)) {
        return false;
      }
      Lexer lookahead = lexer_;
      std::size_t depth = 1U;
      while (depth != 0U) {
        const Token token = lookahead.take();
        if (token.kind == TokenKind::End || token.kind == TokenKind::Invalid) {
          return false;
        }
        if (token.kind == TokenKind::LeftParen) {
          ++depth;
        } else if (token.kind == TokenKind::RightParen) {
          --depth;
        }
      }
      return lookahead.take().kind == TokenKind::Arrow;
    };
    if (starts_function_type()) {
      result.push_back(type_expression(generics));
    } else if (match(TokenKind::LeftParen)) {
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

  void parse_type() {
    const SourcePosition begin = current_.begin;
    auto definition_name = name("a type name");
    auto definition_parameters = parameters(false, true);
    std::vector<Module::TypeDecl::DerivedParameterDecl> derived_parameters;
    if (!match(TokenKind::Semicolon)) {
      expect(TokenKind::LeftBrace, "'{'");
      while (!is(TokenKind::RightBrace) && ok()) {
        auto field = name("a derived parameter name");
        expect(TokenKind::Colon, "':'");
        auto field_domain = parameter_domain();
        expect(TokenKind::Equal, "'='");
        std::vector<ParsedModule::GenericDefinition> variables;
        variables.reserve(definition_parameters.size());
        for (const auto& parameter : definition_parameters) {
          variables.push_back({parameter.name, parameter.domain});
        }
        auto body = type_expression(variables);
        expect(TokenKind::Semicolon, "';'");
        if (field && field_domain) {
          derived_parameters.push_back(
              {std::move(*field),
               detail::domain_expression(field_domain->element,
                                         field_domain->list),
               std::move(body)});
        }
      }
      expect(TokenKind::RightBrace, "'}'");
    }
    if (definition_name) {
      module_.types.push_back(
          {std::move(*definition_name), std::move(definition_parameters),
           std::move(derived_parameters),
           SourceRange{source_, begin, current_.begin}});
    }
  }

  void parse_function() {
    const SourcePosition begin = current_.begin;

    std::optional<Module::FunctionDecl::Fixity> declared_fixity;
    expect_name("fn");
    if (match_name("prefix")) {
      declared_fixity = Module::FunctionDecl::Fixity::Prefix;
    } else if (match_name("infix")) {
      declared_fixity = Module::FunctionDecl::Fixity::Infix;
    } else if (match_name("postfix")) {
      declared_fixity = Module::FunctionDecl::Fixity::Postfix;
    }
    const bool symbolic = is(TokenKind::LeftParen);
    std::optional<std::string> function_name;
    if (symbolic) {
      advance();
      function_name = operator_symbol();
      expect(TokenKind::RightParen, "')'");
    } else {
      function_name = name("a function name");
      if (declared_fixity) {
        error("operator fixity requires a symbolic function name");
      }
    }
    auto generics = function_generics();
    std::vector<Parameter> inputs;
    std::vector<std::optional<ParsedModule::TypeExpression>> input_bindings;
    expect(TokenKind::LeftParen, "'('");
    if (!match(TokenKind::RightParen)) {
      do {
        auto input_name = name("a parameter name");
        expect(TokenKind::Colon, "':'");
        Parameter input;
        if (input_name) {
          input.name = std::move(*input_name);
        }
        input.domain = type_expression(generics);
        const bool ir_input = is_ir_port(input.domain, generics);
        std::optional<ParsedModule::TypeExpression> binding;
        if (!ir_input && !detail::kernel_domain(input.domain)) {
          const auto* generic =
              input.domain.kind == ParsedModule::TypeExpression::Kind::Variable
                  ? find_generic(generics, input.domain.text)
                  : nullptr;
          if (generic == nullptr) {
            error("a compile-time parameter annotation must name its domain "
                  "or a compatible generic");
          } else {
            binding = input.domain;
            input.domain = generic->domain;
          }
        }
        if (match(TokenKind::Ellipsis)) {
          input.variadic = true;
          if (!ir_input) {
            error("a compile-time parameter uses list<D> rather than "
                  "variadic syntax");
          }
        }
        if (match(TokenKind::Equal)) {
          const auto domain = detail::kernel_domain(input.domain);
          if (ir_input || !domain || domain->list ||
              domain->element == ValueKind::Function ||
              domain->element == ValueKind::Bytes || input.variadic) {
            error("this parameter cannot have a default value");
          } else {
            input.default_value = literal(domain->element);
          }
        }
        inputs.push_back(std::move(input));
        input_bindings.push_back(std::move(binding));
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }

    std::vector<ParsedModule::TypeExpression> result_types;
    if (match(TokenKind::Arrow)) {
      result_types = function_results(generics);
    }
    ParsedModule::FunctionDefinition definition;
    if (function_name) {
      definition.name = std::move(*function_name);
    }
    definition.generics = generics;
    definition.types.generics = std::move(generics);
    definition.inputs = std::move(inputs);
    definition.types.bindings = std::move(input_bindings);
    for (std::size_t index = 0; index < result_types.size(); ++index) {
      if (!is_ir_port(result_types[index], definition.generics) &&
          !detail::kernel_domain(result_types[index])) {
        const auto* generic =
            result_types[index].kind ==
                    ParsedModule::TypeExpression::Kind::Variable
                ? find_generic(definition.generics, result_types[index].text)
                : nullptr;
        if (generic != nullptr) {
          result_types[index] = generic->domain;
        }
      }
      definition.results.push_back(
          {result_types.size() == 1U ? "result"
                                     : "result" + std::to_string(index),
           std::move(result_types[index]), false, std::nullopt});
    }
    definition.operator_fixity = declared_fixity;
    if (symbolic && !definition.operator_fixity) {
      const auto value_inputs = static_cast<std::size_t>(
          std::count_if(definition.inputs.begin(), definition.inputs.end(),
                        detail::is_value_port));
      const std::size_t operands =
          value_inputs == 0U ? definition.inputs.size() : value_inputs;
      definition.operator_fixity =
          operands == 1U   ? std::optional{Module::FunctionDecl::Fixity::Prefix}
          : operands == 2U ? std::optional{Module::FunctionDecl::Fixity::Infix}
                           : std::nullopt;
    }

    if (match(TokenKind::Semicolon)) {
    } else if (is(TokenKind::LeftBrace)) {
      std::vector<ParsedModule::GenericDefinition> variables =
          definition.generics;
      for (const auto& input : definition.inputs) {
        variables.push_back({input.name, input.domain});
      }
      auto body = detail::parse_function_body(lexer_, current_, diagnostics_,
                                              source_, variables);
      if (!body) {
        return;
      }
      definition.body = std::move(*body);
    } else {
      error("expected ';' or a function body");
    }
    if (!definition.name.empty()) {
      definition.source = SourceRange{source_, begin, current_.begin};
      module_.functions.push_back(std::move(definition));
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
                           std::optional<SourceRange> source,
                           bool allow_value_ports = false) {
    if (!unique_parameter_names(values)) {
      error("duplicate parameter in '" + std::string(owner) + "'", source);
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
      if (!allow_value_ports && detail::is_value_port(values[index])) {
        error("unknown parameter domain on '" + values[index].name + "' in '" +
                  std::string(owner) + "'",
              source);
      }
      if (values[index].variadic && index + 1U != values.size()) {
        error("variadic parameter must be last in '" + std::string(owner) + "'",
              source);
      }
    }
  }

  void validate_declaration_expression(
      std::span<const ParsedModule::GenericDefinition> variables,
      std::string_view owner, std::optional<SourceRange> source,
      const ParsedModule::TypeExpression& expression,
      const Module::Expression& expected) {
    const auto report = [&](std::string message) {
      error(std::move(message), source);
    };
    using Kind = ParsedModule::TypeExpression::Kind;
    const auto domain = detail::kernel_domain(expected);
    if (!domain) {
      report("unknown parameter domain in " + std::string(owner));
      return;
    }
    if (expression.kind == Kind::FunctionType) {
      const auto signature = detail::callable_type(expression);
      if (domain->list || domain->element != ValueKind::Type || !signature) {
        report("malformed function type in " + std::string(owner));
        return;
      }
      const auto type_domain = detail::domain_expression(ValueKind::Type);
      for (const auto side : {signature->inputs, signature->results}) {
        for (const auto& element : side) {
          validate_declaration_expression(variables, owner, source, element,
                                          type_domain);
        }
      }
      return;
    }
    if (expression.kind == Kind::Variable) {
      const auto* variable = find_generic(variables, expression.text);
      if (variable == nullptr || variable->domain != expected) {
        report("type variable '" + expression.text +
               "' has the wrong domain in " + std::string(owner));
      }
      return;
    }
    if (expression.kind == Kind::Evaluate) {
      if (expression.arguments.size() != 1U) {
        report("malformed compile-time expression in " + std::string(owner));
        return;
      }
      validate_declaration_expression(variables, owner, source,
                                      expression.arguments.front(), expected);
      return;
    }
    if (expression.kind == Kind::If) {
      if (expression.arguments.size() != 3U) {
        report("malformed if expression in " + std::string(owner));
        return;
      }
      validate_declaration_expression(
          variables, owner, source, expression.arguments[0],
          detail::domain_expression(ValueKind::Boolean));
      validate_declaration_expression(variables, owner, source,
                                      expression.arguments[1], expected);
      validate_declaration_expression(variables, owner, source,
                                      expression.arguments[2], expected);
      return;
    }
    if (domain->list && expression.kind != Kind::Call &&
        expression.kind != Kind::Reference) {
      if (expression.kind != Kind::List) {
        report("expected a list expression in " + std::string(owner));
        return;
      }
      const auto element_domain =
          detail::domain_expression(domain->element, false);
      for (const auto& element : expression.arguments) {
        validate_declaration_expression(variables, owner, source, element,
                                        element_domain);
      }
      return;
    }
    if (expression.kind == Kind::List) {
      report("unexpected list expression in " + std::string(owner));
      return;
    }
    if (expression.kind == Kind::Reference) {
      const std::size_t field_dot = expression.text.find('.');
      if (field_dot != std::string::npos) {
        const std::string_view receiver(expression.text.data(), field_dot);
        const auto* generic = find_generic(variables, receiver);
        if (generic != nullptr) {
          // The concrete type bound to the generic owns and types this field.
          // Resolution checks it once that binding is known.
          return;
        }
      }
    }
    const bool operator_expression = expression.kind == Kind::Prefix ||
                                     expression.kind == Kind::Infix ||
                                     expression.kind == Kind::Postfix;
    if (operator_expression) {
      const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
      if (expression.arguments.size() != arity) {
        report("malformed operator expression in " + std::string(owner));
        return;
      }
      const auto fixity = expression.kind == Kind::Prefix
                              ? Module::FunctionDecl::Fixity::Prefix
                          : expression.kind == Kind::Postfix
                              ? Module::FunctionDecl::Fixity::Postfix
                              : Module::FunctionDecl::Fixity::Infix;
      std::vector<const ParsedModule::FunctionDefinition*> candidates;
      for (const auto& candidate : module_.functions) {
        if (candidate.name == expression.text &&
            candidate.operator_fixity == fixity &&
            candidate.inputs.size() == arity &&
            candidate.results.size() == 1U &&
            candidate.results.front().domain == expected) {
          candidates.push_back(&candidate);
        }
      }
      candidates.erase(
          std::remove_if(
              candidates.begin(), candidates.end(),
              [&](const ParsedModule::FunctionDefinition* candidate) {
                for (std::size_t index = 0; index < arity; ++index) {
                  const auto& argument = expression.arguments[index];
                  const auto* variable =
                      argument.kind == Kind::Variable
                          ? find_generic(variables, argument.text)
                          : nullptr;
                  if (variable != nullptr &&
                      candidate->inputs[index].domain != variable->domain) {
                    return true;
                  }
                }
                return false;
              }),
          candidates.end());
      if (candidates.size() == 1U) {
        for (std::size_t index = 0; index < arity; ++index) {
          validate_declaration_expression(
              variables, owner, source, expression.arguments[index],
              candidates.front()->inputs[index].domain);
        }
        return;
      }
      // Imports, the ambient Prelude, and overload ambiguity are resolved
      // after the complete Module closure is available to Compiler::link.
      return;
    }
    if (expression.kind == Kind::Call) {
      if (!reference_is_visible(expression.text, "function", source)) {
        return;
      }
      // Calls need the complete import closure, overload set, labels, and
      // defaults. Compiler::link performs that semantic check once Modules
      // have immutable declarations; parsing only checks visibility here.
      return;
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      const bool matches = (expression.kind == Kind::Number &&
                            (domain->element == ValueKind::Integer ||
                             domain->element == ValueKind::Real)) ||
                           (expression.kind == Kind::Boolean &&
                            domain->element == ValueKind::Boolean) ||
                           (expression.kind == Kind::String &&
                            domain->element == ValueKind::String);
      if (!matches) {
        report("literal has the wrong domain in " + std::string(owner));
      }
      return;
    }
    if (!reference_is_visible(expression.text, "type", source)) {
      return;
    }
    if (domain->element != ValueKind::Type) {
      report("type expression reference has the wrong domain in " +
             std::string(owner));
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
    const auto declaration = std::find_if(
        module_.types.begin(), module_.types.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    if (declaration == module_.types.end()) {
      report("unknown type '" + expression.text + "' in " + std::string(owner));
      return;
    }
    if (expression.arguments.size() > declaration->parameters.size()) {
      report("too many arguments for type '" + expression.text + "'");
      return;
    }
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      validate_declaration_expression(variables, owner, source,
                                      expression.arguments[index],
                                      declaration->parameters[index].domain);
    }
    for (std::size_t index = expression.arguments.size();
         index < declaration->parameters.size(); ++index) {
      if (!declaration->parameters[index].default_value) {
        report("missing argument '" + declaration->parameters[index].name +
               "' for type '" + expression.text + "'");
      }
    }
  }

  bool reference_is_visible(std::string_view reference, std::string_view kind,
                            std::optional<SourceRange> source = std::nullopt) {
    const std::size_t dot = reference.find('.');
    if (dot == std::string_view::npos) {
      return true;
    }
    const std::string_view module_name = reference.substr(0, dot);
    if (module_name == detail::prelude_module_name) {
      return true;
    }
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
    check_unique(module_.types, "type");
    for (std::size_t left = 0; left < module_.functions.size(); ++left) {
      const auto& function = module_.functions[left];
      for (std::size_t right = left + 1U; right < module_.functions.size();
           ++right) {
        const auto& candidate = module_.functions[right];
        if (function.name != candidate.name ||
            function.generics.size() != candidate.generics.size() ||
            function.inputs.size() != candidate.inputs.size() ||
            function.results.size() != candidate.results.size()) {
          continue;
        }
        const bool same_inputs = std::equal(
            function.inputs.begin(), function.inputs.end(),
            candidate.inputs.begin(), [](const auto& lhs, const auto& rhs) {
              return lhs.variadic == rhs.variadic && lhs.domain == rhs.domain;
            });
        const bool same_results = std::equal(
            function.results.begin(), function.results.end(),
            candidate.results.begin(), [](const auto& lhs, const auto& rhs) {
              return lhs.domain == rhs.domain;
            });
        if (same_inputs && same_results) {
          error("duplicate function overload '" + function.name + "'",
                candidate.source);
        }
      }
    }
    for (const auto& type : module_.types) {
      validate_parameters(type.parameters, type.name, type.source);
      std::unordered_set<std::string> derived_names;
      for (const auto& derived : type.derived_parameters) {
        if (!derived_names.insert(derived.name).second) {
          error("duplicate derived parameter '" + derived.name + "' on type '" +
                    type.name + "'",
                type.source);
        }
        const auto shadows =
            std::find_if(type.parameters.begin(), type.parameters.end(),
                         [&](const auto& parameter) {
                           return parameter.name == derived.name;
                         });
        if (shadows != type.parameters.end()) {
          error("derived parameter '" + derived.name + "' on type '" +
                    type.name + "' shadows a constructor parameter",
                type.source);
        }
      }
    }
    for (const auto& function : module_.functions) {
      validate_parameters(function.inputs, function.name, function.source,
                          true);
      validate_parameters(function.results, function.name, function.source,
                          true);
      std::unordered_set<std::string> input_names;
      for (std::size_t index = 0; index < function.inputs.size(); ++index) {
        const auto& input = function.inputs[index];
        if (!input_names.insert(input.name).second) {
          error("duplicate function input '" + input.name + "' in '" +
                    function.name + "'",
                function.source);
        }
        if (input.variadic && index + 1U != function.inputs.size()) {
          error("variadic input must be last in '" + function.name + "'",
                function.source);
        }
      }
      std::unordered_set<std::string> generics;
      for (const auto& generic : function.generics) {
        if (!generics.insert(generic.name).second) {
          error("duplicate type variable '" + generic.name + "' in '" +
                    function.name + "'",
                function.source);
        }
      }
      const auto type_domain = detail::domain_expression(ValueKind::Type);
      const std::string owner = "function '" + function.name + "'";
      for (std::size_t index = 0; index < function.inputs.size(); ++index) {
        const auto& input = function.inputs[index];
        if (detail::is_value_port(input)) {
          validate_declaration_expression(function.generics, owner,
                                          function.source, input.domain,
                                          type_domain);
        }
      }
      if (!function.types.bindings.empty()) {
        if (function.types.bindings.size() != function.inputs.size()) {
          error("function '" + function.name +
                    "' has an invalid static binding contract",
                function.source);
        } else {
          for (std::size_t index = 0; index < function.inputs.size(); ++index) {
            if (function.types.bindings[index]) {
              validate_declaration_expression(function.generics, owner,
                                              function.source,
                                              *function.types.bindings[index],
                                              function.inputs[index].domain);
            }
          }
        }
      }
      for (std::size_t index = 0; index < function.results.size(); ++index) {
        const auto& result = function.results[index];
        const auto expected =
            detail::is_value_port(result) ? type_domain : result.domain;
        if (detail::is_value_port(result)) {
          validate_declaration_expression(function.generics, owner,
                                          function.source, result.domain,
                                          expected);
        }
      }
      if (function.operator_fixity) {
        const auto module_values = static_cast<std::size_t>(
            std::count_if(function.inputs.begin(), function.inputs.end(),
                          detail::is_value_port));
        const std::size_t operands =
            module_values == 0U ? function.inputs.size() : module_values;
        const std::size_t required =
            function.operator_fixity == Module::FunctionDecl::Fixity::Infix
                ? 2U
                : 1U;
        const bool expected_ir_result = module_values != 0U;
        if (!function.operator_fixity || operands != required ||
            function.results.size() != 1U ||
            detail::is_value_port(function.results.front()) !=
                expected_ir_result) {
          error("operator on function '" + function.name +
                    "' has an incompatible signature",
                function.source);
        }
      }
      if (const Module::Expression* expression = body_expression(function.body);
          expression != nullptr && function.results.size() == 1U &&
          !detail::is_value_port(function.results.front()) &&
          std::none_of(function.inputs.begin(), function.inputs.end(),
                       detail::is_value_port)) {
        std::vector<ParsedModule::GenericDefinition> variables =
            function.generics;
        for (std::size_t index = 0; index < function.inputs.size(); ++index) {
          const auto& input = function.inputs[index];
          if (!detail::is_value_port(input)) {
            variables.push_back({input.name, input.domain});
          }
        }
        validate_declaration_expression(variables, owner, function.source,
                                        *expression,
                                        function.results.front().domain);
      }
    }
  }

  void error(std::string message) {
    diagnostics_.report(std::move(message),
                        SourceRange{source_, current_.begin, current_.end});
  }

  void error(std::string message, SourceRange source) {
    diagnostics_.report(std::move(message), std::move(source));
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
  Diagnostics diagnostics_;
};

std::string symbol_kind_name(Module::SymbolKind kind) {
  switch (kind) {
  case Module::SymbolKind::Type:
    return "type";
  case Module::SymbolKind::Function:
    return "fn";
  }
  return "invalid";
}

std::string type_expression_text(const detail::TypeExpression& expression,
                                 int parent_precedence = 0,
                                 bool right_operand = false);

std::string parameter_text(const Parameter& parameter) {
  std::string result =
      parameter.name + ": " + type_expression_text(parameter.domain);
  if (parameter.variadic) {
    result += "...";
  }
  if (parameter.default_value) {
    result += " = " + type_expression_text(*parameter.default_value);
  }
  return result;
}

std::string parameter_text(const Parameter& parameter,
                           const detail::TypeExpression& annotation) {
  Parameter displayed = parameter;
  displayed.domain = annotation;
  return parameter_text(displayed);
}

int operator_precedence(std::string_view symbol) {
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

int type_expression_precedence(const detail::TypeExpression& expression) {
  using Kind = detail::TypeExpression::Kind;
  switch (expression.kind) {
  case Kind::FunctionType:
    return 1;
  case Kind::If:
    return 5;
  case Kind::Infix:
    return operator_precedence(expression.text);
  case Kind::Prefix:
    return 70;
  case Kind::Postfix:
    return 80;
  case Kind::Evaluate:
    return 90;
  case Kind::Number:
  case Kind::Boolean:
  case Kind::String:
  case Kind::List:
  case Kind::Reference:
  case Kind::Variable:
  case Kind::Call:
    return 100;
  }
  return 0;
}

std::string type_expression_text(const detail::TypeExpression& expression,
                                 int parent_precedence, bool right_operand) {
  return detail::format_expression(expression, parent_precedence,
                                   right_operand);
}

constexpr std::size_t canonical_line_width = 88U;

std::string indentation(std::size_t width) { return std::string(width, ' '); }

void write_indented(std::ostringstream& output, std::string_view source) {
  std::size_t begin = 0;
  while (begin < source.size()) {
    const std::size_t end = source.find('\n', begin);
    const std::string_view line = source.substr(
        begin,
        end == std::string_view::npos ? source.size() - begin : end - begin);
    if (line.find_first_not_of(" \t\r") != std::string_view::npos) {
      output << "  " << line;
    }
    output << '\n';
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
}

std::string type_expression_layout(const detail::TypeExpression& expression,
                                   std::size_t column,
                                   int parent_precedence = 0,
                                   bool right_operand = false) {
  const std::string flat =
      type_expression_text(expression, parent_precedence, right_operand);
  if (column + flat.size() <= canonical_line_width) {
    return flat;
  }

  using Kind = detail::TypeExpression::Kind;
  const int precedence = type_expression_precedence(expression);
  const bool parenthesized =
      precedence < parent_precedence ||
      (right_operand && precedence == parent_precedence && precedence < 100);
  const std::size_t content_column = column + (parenthesized ? 1U : 0U);
  std::string result = parenthesized ? "(" : "";

  if (expression.kind == Kind::If && expression.arguments.size() == 3U) {
    result += "if ";
    result +=
        type_expression_layout(expression.arguments[0], content_column + 3U);
    result += " {\n";
    result += indentation(content_column + 2U);
    result +=
        type_expression_layout(expression.arguments[1], content_column + 2U);
    result += "\n" + indentation(content_column) + "} else {\n";
    result += indentation(content_column + 2U);
    result +=
        type_expression_layout(expression.arguments[2], content_column + 2U);
    result += "\n" + indentation(content_column) + "}";
  } else {
    const bool delimited =
        expression.kind == Kind::List || expression.kind == Kind::Reference ||
        expression.kind == Kind::Call || expression.kind == Kind::Evaluate;
    if (delimited && !expression.arguments.empty()) {
      const std::string opening =
          expression.kind == Kind::List       ? "["
          : expression.kind == Kind::Evaluate ? "@("
          : expression.kind == Kind::Call
              ? expression.text + "("
              : std::string(detail::display_type_name(expression.text)) + "<";
      const char closing =
          expression.kind == Kind::List ? ']'
          : expression.kind == Kind::Call || expression.kind == Kind::Evaluate
              ? ')'
              : '>';
      result += opening + '\n';
      const std::size_t argument_column = content_column + 2U;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        result += indentation(argument_column);
        const bool labeled = expression.kind == Kind::Call &&
                             index < expression.labels.size() &&
                             !expression.labels[index].empty();
        if (labeled) {
          result += expression.labels[index] + ": ";
        }
        result += type_expression_layout(
            expression.arguments[index],
            argument_column +
                (labeled ? expression.labels[index].size() + 2U : 0U));
        if (index + 1U != expression.arguments.size()) {
          result += ',';
        }
        result += '\n';
      }
      result += indentation(content_column);
      result.push_back(closing);
    } else if (expression.kind == Kind::Prefix) {
      result += expression.text;
      result += type_expression_layout(expression.arguments.front(),
                                       content_column + expression.text.size(),
                                       precedence, true);
    } else if (expression.kind == Kind::Postfix) {
      result += type_expression_layout(expression.arguments.front(),
                                       content_column, precedence, false);
      result += expression.text;
    } else if (expression.kind == Kind::Infix) {
      result += type_expression_layout(expression.arguments[0], content_column,
                                       precedence, false);
      result += " " + expression.text;
      result += '\n';
      const std::size_t rhs_column = content_column + 2U;
      result += indentation(rhs_column);
      result += type_expression_layout(expression.arguments[1], rhs_column,
                                       precedence, true);
    } else {
      return flat;
    }
  }

  if (parenthesized) {
    result += ')';
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
                       std::string declaration_digest, SymbolKind kind,
                       std::string local_name, std::string discriminator)
    : module_name_(std::move(module_name)), module_version_(module_version),
      declaration_digest_(std::move(declaration_digest)), kind_(kind),
      local_name_(std::move(local_name)),
      discriminator_(std::move(discriminator)) {}

std::string Module::Symbol::qualified_name() const {
  return module_name_ + "." + local_name_;
}

std::string Module::Symbol::stable_name() const {
  std::string result = module_name_ + "@" + to_string(module_version_) + "/" +
                       symbol_kind_name(kind_) + "/" + local_name_;
  if (!discriminator_.empty()) {
    result += "/" + discriminator_;
  }
  return result;
}

bool Module::Symbol::operator==(const Symbol& other) const {
  return module_name_ == other.module_name_ &&
         module_version_ == other.module_version_ && kind_ == other.kind_ &&
         local_name_ == other.local_name_ &&
         discriminator_ == other.discriminator_;
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

std::span<const Module::TypeDecl::DerivedParameterDecl>
Module::TypeDecl::derived_parameters() const {
  return storage_->types[index_].derived_parameters;
}

Module::Symbol Module::TypeDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->declaration_digest,
          SymbolKind::Type,
          storage_->types[index_].name};
}

bool Module::TypeDecl::operator==(const TypeDecl& other) const {
  return symbol() == other.symbol();
}

Module::FunctionDecl::FunctionDecl(std::shared_ptr<const Storage> storage,
                                   std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Module::FunctionDecl::name() const {
  return storage_->functions[index_].name;
}

std::span<const Module::FunctionDecl::GenericDecl>
Module::FunctionDecl::generics() const {
  return storage_->functions[index_].declaration->generics;
}

std::span<const Module::ParameterDecl> Module::FunctionDecl::inputs() const {
  return storage_->functions[index_].declaration->inputs;
}

std::span<const Module::ParameterDecl> Module::FunctionDecl::results() const {
  return storage_->functions[index_].declaration->results;
}

namespace {
std::vector<Module::ParameterDecl>
select_parameters(std::span<const Module::ParameterDecl> parameters,
                  bool select_values) {
  std::vector<Module::ParameterDecl> result;
  for (const Module::ParameterDecl& parameter : parameters) {
    if (detail::is_value_port(parameter) == select_values) {
      result.push_back(parameter);
    }
  }
  return result;
}
}  // namespace

bool detail::is_value_port(const Module::ParameterDecl& parameter) {
  return !kernel_domain(parameter.domain);
}

std::vector<Module::ParameterDecl> detail::FunctionTypeAccess::compiler_inputs(
    const Module::FunctionDecl& function) {
  return select_parameters(function.inputs(), false);
}

std::vector<Module::ParameterDecl>
detail::FunctionTypeAccess::value_inputs(const Module::FunctionDecl& function) {
  return select_parameters(function.inputs(), true);
}

std::vector<Module::ParameterDecl> detail::FunctionTypeAccess::compiler_results(
    const Module::FunctionDecl& function) {
  return select_parameters(function.results(), false);
}

std::vector<Module::ParameterDecl> detail::FunctionTypeAccess::value_results(
    const Module::FunctionDecl& function) {
  return select_parameters(function.results(), true);
}

bool detail::has_default_specialization(const Module::FunctionDecl& function) {
  const auto& contract = FunctionTypeAccess::get(function);
  std::vector<std::string_view> bound_generics;
  for (std::size_t index = 0; index < function.inputs().size(); ++index) {
    if (is_value_port(function.inputs()[index])) {
      continue;
    }
    if (!function.inputs()[index].default_value) {
      return false;
    }
    if (index < contract.bindings.size() && contract.bindings[index] &&
        contract.bindings[index]->kind == Module::Expression::Kind::Variable) {
      bound_generics.push_back(contract.bindings[index]->text);
    }
  }
  return std::all_of(function.generics().begin(), function.generics().end(),
                     [&](const auto& generic) {
                       return std::find(bound_generics.begin(),
                                        bound_generics.end(),
                                        generic.name) != bound_generics.end();
                     });
}

std::optional<Module::FunctionDecl::Fixity>
Module::FunctionDecl::operator_fixity() const {
  return storage_->functions[index_].declaration->operator_fixity;
}

Module::FunctionDecl::Form Module::FunctionDecl::form() const {
  const detail::FunctionMember& function = storage_->functions[index_];
  return function.ir || function.declaration->body ? Form::Body
                                                   : Form::External;
}

const Function* Module::FunctionDecl::body() const {
  return storage_->functions[index_].ir.get();
}

const Module::Expression*
detail::ModuleAccess::expression(const Module::FunctionDecl& function) {
  if (!FunctionTypeAccess::value_inputs(function).empty() ||
      !FunctionTypeAccess::value_results(function).empty()) {
    return nullptr;
  }
  return returned_expression(function);
}

const Module::Expression* detail::ModuleAccess::returned_expression(
    const Module::FunctionDecl& function) {
  const auto& body =
      function.storage_->functions[function.index_].declaration->body;
  if (!body || body->blocks.size() != 1U || body->blocks.front().terminator ||
      body->blocks.front().statements.size() != 1U ||
      body->blocks.front().statements.front().kind !=
          detail::StatementSyntax::Kind::Return ||
      body->blocks.front().statements.front().values.size() != 1U) {
    return nullptr;
  }
  return &body->blocks.front().statements.front().values.front().value;
}

std::string Module::FunctionDecl::signature() const {
  std::string result(name());
  if (!generics().empty()) {
    result += '<';
    for (std::size_t index = 0; index < generics().size(); ++index) {
      if (index != 0U) {
        result += ',';
      }
      result += type_expression_text(generics()[index].domain);
    }
    result += '>';
  }
  result += '(';
  for (std::size_t index = 0; index < inputs().size(); ++index) {
    if (index != 0U) {
      result += ',';
    }
    result += type_expression_text(inputs()[index].domain);
    if (inputs()[index].variadic) {
      result += "...";
    }
  }
  result += ")->";
  if (results().size() != 1U) {
    result += '(';
  }
  for (std::size_t index = 0; index < results().size(); ++index) {
    if (index != 0U) {
      result += ',';
    }
    result += type_expression_text(results()[index].domain);
  }
  if (results().size() != 1U) {
    result += ')';
  }
  return result;
}

Module::Symbol Module::FunctionDecl::symbol() const {
  return {storage_->name,
          storage_->version,
          storage_->declaration_digest,
          SymbolKind::Function,
          storage_->functions[index_].name,
          signature()};
}

bool Module::FunctionDecl::operator==(const FunctionDecl& other) const {
  return symbol() == other.symbol();
}

const detail::FunctionTypeContract&
detail::FunctionTypeAccess::get(const Module::FunctionDecl& function) {
  return function.storage_->functions[function.index_].declaration->types;
}

Module::Module(std::shared_ptr<const Storage> storage)
    : storage_(std::move(storage)) {}

Module::Module(std::string name, Version version) {
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_') ||
      !std::all_of(name.begin() + 1, name.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
               character == '_';
      })) {
    throw std::invalid_argument("a Module needs a valid name");
  }
  auto storage = std::make_shared<Storage>();
  storage->name = std::move(name);
  storage->version = version;
  storage_ = storage;
  storage->digest = compute_digest(storage);
  storage->declaration_digest = compute_declaration_digest(storage);
}

Module::Module(const Module& other) : storage_(other.storage_) {
  if (std::none_of(storage_->functions.begin(), storage_->functions.end(),
                   [](const detail::FunctionMember& function) {
                     return function.ir != nullptr;
                   })) {
    return;
  }
  auto storage = std::make_shared<Storage>(*storage_);
  for (detail::FunctionMember& function : storage->functions) {
    if (function.ir) {
      function.ir = std::make_shared<Function>(*function.ir);
    }
  }
  storage_ = std::move(storage);
}

Module& Module::operator=(const Module& other) {
  if (this != &other) {
    Module copy(other);
    storage_.swap(copy.storage_);
  }
  return *this;
}

std::string_view Module::name() const { return storage_->name; }

Version Module::version() const { return storage_->version; }

std::string Module::compute_digest(
    const std::shared_ptr<const Storage>& storage) {
  std::string canonical = format(Module(storage));
  for (const auto& [name, payload] : storage->data) {
    static_cast<void>(payload);
    canonical += "\ndata ";
    canonical += name;
    canonical += ';';
  }
  return sha256(canonical);
}

std::string_view
Module::current_digest(const std::shared_ptr<const Storage>& storage) {
  if (std::any_of(storage->functions.begin(), storage->functions.end(),
                  [](const detail::FunctionMember& function) {
                    return function.ir != nullptr;
                  })) {
    std::vector<std::pair<std::string, Function::Revision>> revisions;
    revisions.reserve(storage->functions.size());
    for (const detail::FunctionMember& function : storage->functions) {
      if (function.ir) {
        revisions.emplace_back(function.name, function.ir->revision());
      }
    }
    if (revisions != storage->digest_revisions) {
      storage->digest = compute_digest(storage);
      storage->digest_revisions = std::move(revisions);
    }
  }
  return storage->digest;
}

std::string
Module::compute_declaration_digest(const std::shared_ptr<const Storage>& storage) {
  return std::string(declaration_view(storage).digest());
}

Module
Module::declaration_view(const std::shared_ptr<const Storage>& storage) {
  auto declarations = std::make_shared<Storage>(*storage);
  const Module source(storage);
  for (const Dependency& dependency : source.dependencies()) {
    if (dependency.name == detail::prelude_module_name ||
        dependency.name == source.name()) {
      continue;
    }
    const auto found = std::find_if(
        declarations->imports.begin(), declarations->imports.end(),
        [&](const Import& import) { return import.name == dependency.name; });
    if (found == declarations->imports.end()) {
      declarations->imports.push_back(
          {dependency.name,
           {VersionRangeKind::Exact, dependency.version},
           {}});
    } else if (found->alias.empty()) {
      found->version = {VersionRangeKind::Exact, dependency.version};
    }
  }
  for (detail::FunctionMember& member : declarations->functions) {
    if (member.declaration) {
      member.declaration->body.reset();
    }
    member.ir.reset();
  }
  declarations->data.clear();
  std::vector<std::size_t> order(declarations->functions.size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(), [&](std::size_t left,
                                            std::size_t right) {
    return FunctionDecl(declarations, left).signature() <
           FunctionDecl(declarations, right).signature();
  });
  std::vector<detail::FunctionMember> functions;
  functions.reserve(order.size());
  for (const std::size_t index : order) {
    functions.push_back(std::move(declarations->functions[index]));
  }
  declarations->functions = std::move(functions);
  declarations->digest.clear();
  declarations->declaration_digest.clear();
  declarations->digest_revisions.clear();
  Module result(declarations);
  declarations->digest = compute_digest(declarations);
  declarations->declaration_digest = declarations->digest;
  return result;
}

std::string_view Module::digest() const {
  return current_digest(storage_);
}

std::string_view Module::declaration_digest() const {
  return storage_->declaration_digest;
}

std::span<const Module::Import> Module::imports() const {
  return storage_->imports;
}

std::string Module::store(Bytes bytes) {
  const std::string_view raw(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size());
  const std::string name = "sha256:" + sha256(raw);
  const auto existing = storage_->data.find(name);
  if (existing != storage_->data.end()) {
    if (*existing->second != bytes) {
      throw std::logic_error("content-addressed Module data collision");
    }
    return name;
  }
  auto next = std::make_shared<Storage>(*storage_);
  next->data.emplace(name,
                     std::make_shared<const Bytes>(std::move(bytes)));
  next->digest = compute_digest(next);
  storage_ = std::move(next);
  return name;
}

std::optional<std::span<const std::byte>>
Module::data(std::string_view name) const {
  const auto found = storage_->data.find(name);
  return found == storage_->data.end()
             ? std::nullopt
             : std::optional<std::span<const std::byte>>{*found->second};
}

std::vector<std::string> Module::data() const {
  std::vector<std::string> names;
  names.reserve(storage_->data.size());
  for (const auto& [name, payload] : storage_->data) {
    static_cast<void>(payload);
    names.push_back(name);
  }
  return names;
}

std::optional<Module::TypeDecl> Module::type(std::string_view name) const {
  const auto index = find_definition(storage_->types, name);
  return index ? std::optional<TypeDecl>{TypeDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Module::FunctionDecl>
Module::function(std::string_view name) const {
  const auto values = overloads(name);
  return values.size() == 1U ? std::optional<FunctionDecl>{values.front()}
                             : std::nullopt;
}

std::vector<Module::FunctionDecl>
Module::overloads(std::string_view name) const {
  std::vector<FunctionDecl> result;
  for (std::size_t index = 0; index < storage_->functions.size(); ++index) {
    if (storage_->functions[index].declaration &&
        storage_->functions[index].name == name) {
      result.push_back(FunctionDecl(storage_, index));
    }
  }
  return result;
}

std::optional<Module::Symbol> Module::symbol(SymbolKind kind,
                                             std::string_view name) const {
  if (kind == SymbolKind::Function) {
    const auto declarations = overloads(name);
    return declarations.size() == 1U
               ? std::optional<Symbol>{declarations.front().symbol()}
               : std::nullopt;
  }
  const bool exists = kind == SymbolKind::Type && type(name).has_value();
  if (!exists) {
    return std::nullopt;
  }
  return Symbol(std::string(storage_->name), storage_->version,
                storage_->declaration_digest, kind, std::string(name));
}

std::vector<Module::Symbol> Module::members() const {
  std::vector<Symbol> result;
  result.reserve(storage_->types.size() + storage_->functions.size());
  const auto append = [&](SymbolKind kind, const auto& definitions) {
    for (const auto& definition : definitions) {
      result.push_back(Symbol(std::string(storage_->name), storage_->version,
                              storage_->declaration_digest, kind,
                              std::string(definition.name)));
    }
  };
  append(SymbolKind::Type, storage_->types);
  for (std::size_t index = 0; index < storage_->functions.size(); ++index) {
    if (storage_->functions[index].declaration) {
      result.push_back(FunctionDecl(storage_, index).symbol());
    }
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

std::vector<Module::FunctionDecl> Module::functions() const {
  std::vector<FunctionDecl> result;
  result.reserve(storage_->functions.size());
  for (std::size_t index = 0; index < storage_->functions.size(); ++index) {
    if (storage_->functions[index].declaration) {
      result.push_back(FunctionDecl(storage_, index));
    }
  }
  return result;
}

bool Module::operator==(const Module& other) const {
  return name() == other.name() && version() == other.version() &&
         digest() == other.digest();
}

Module detail::ModuleAccess::declaration_view(const Module& module) {
  return Module::declaration_view(module.storage_);
}

std::shared_ptr<const detail::FunctionBody>
detail::ModuleAccess::body(const Module& module,
                           const Module::FunctionDecl& function) {
  if (function.storage_.get() != module.storage_.get() ||
      function.index_ >= module.storage_->functions.size()) {
    return nullptr;
  }
  const auto& declaration =
      module.storage_->functions[function.index_].declaration;
  if (!declaration) {
    return nullptr;
  }
  const auto& body = declaration->body;
  if (!body) {
    return nullptr;
  }
  return std::shared_ptr<const detail::FunctionBody>(module.storage_, &*body);
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
  case Module::SymbolKind::Type:
    return find_source(module.storage_->types);
  case Module::SymbolKind::Function: {
    const auto found = std::find_if(
        module.storage_->functions.begin(), module.storage_->functions.end(),
        [&](const detail::FunctionMember& function) {
          return function.declaration && function.name == name;
        });
    return found != module.storage_->functions.end()
               ? found->declaration->source
               : std::optional<SourceRange>{};
  }
  }
  return std::nullopt;
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
  storage->types = std::move(parsed->types);
  storage->functions.reserve(parsed->functions.size());
  for (detail::FunctionDefinition& function : parsed->functions) {
    std::string name = function.name;
    storage->functions.push_back(
        {std::move(name), std::move(function), nullptr});
  }
  Module module(storage);
  storage->digest = Module::compute_digest(storage);
  storage->declaration_digest = Module::compute_declaration_digest(storage);
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
  std::vector<Module::Import> imports(module.imports().begin(),
                                      module.imports().end());
  for (const Module::Dependency& dependency : module.dependencies()) {
    if (dependency.name == detail::prelude_module_name ||
        dependency.name == module.name()) {
      continue;
    }
    const auto found =
        std::find_if(imports.begin(), imports.end(), [&](const auto& import) {
          return import.name == dependency.name;
        });
    if (found == imports.end()) {
      imports.push_back(
          {dependency.name, {VersionRangeKind::Exact, dependency.version}, {}});
    } else if (found->alias.empty()) {
      found->version = {VersionRangeKind::Exact, dependency.version};
    }
  }
  std::sort(imports.begin(), imports.end(),
            [](const auto& left, const auto& right) {
              return left.name < right.name;
            });
  for (const Module::Import& import : imports) {
    output << "  import " << import.name << '@' << to_string(import.version);
    if (!import.alias.empty()) {
      output << " as " << import.alias;
    }
    output << ";\n";
  }

  bool wrote_group = !imports.empty();
  const auto begin_group = [&](bool present) {
    if (!present) {
      return;
    }
    if (wrote_group) {
      output << '\n';
    }
    wrote_group = true;
  };
  begin_group(!module.storage_->types.empty());
  for (const auto& type : module.storage_->types) {
    const std::string head = "  type " + type.name;
    std::string flat = head + '(';
    for (std::size_t index = 0; index < type.parameters.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += parameter_text(type.parameters[index]);
    }
    flat += ')';
    if (!type.derived_parameters.empty()) {
      if (flat.size() <= canonical_line_width || type.parameters.empty()) {
        output << flat << " {\n";
      } else {
        output << head << "(\n";
        for (std::size_t index = 0; index < type.parameters.size(); ++index) {
          output << "    " << parameter_text(type.parameters[index]);
          if (index + 1U != type.parameters.size()) {
            output << ',';
          }
          output << '\n';
        }
        output << "  ) {\n";
      }
      for (const auto& derived : type.derived_parameters) {
        const std::string field_head =
            "    " + derived.name + ": " +
            type_expression_text(derived.domain) + " = ";
        const std::string value = type_expression_text(derived.value);
        if (field_head.size() + value.size() <= canonical_line_width) {
          output << field_head << value << ";\n";
        } else {
          output << field_head << "\n      "
                 << type_expression_layout(derived.value, 6U) << ";\n";
        }
      }
      output << "  }\n";
      continue;
    }
    flat += ";\n";
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
    output << "  );\n";
  }
  const bool has_declarations = std::any_of(
      module.storage_->functions.begin(), module.storage_->functions.end(),
      [](const detail::FunctionMember& function) {
        return function.declaration.has_value() && !function.ir;
      });
  begin_group(has_declarations);
  for (const detail::FunctionMember& member : module.storage_->functions) {
    if (!member.declaration || member.ir) {
      continue;
    }
    const detail::FunctionDefinition& function = *member.declaration;
    std::string head = "  fn ";
    if (function.operator_fixity) {
      if (function.operator_fixity == Module::FunctionDecl::Fixity::Postfix) {
        head += "postfix ";
      }
      head += '(' + function.name + ')';
    } else {
      head += function.name;
    }
    if (!function.generics.empty()) {
      std::vector<std::string> generics;
      generics.reserve(function.generics.size());
      for (std::size_t index = 0; index < function.generics.size(); ++index) {
        const auto& generic = function.generics[index];
        std::string text = generic.name;
        if (generic.domain != Module::Expression::reference("type")) {
          text += ": " + type_expression_text(generic.domain);
        }
        generics.push_back(std::move(text));
      }
      std::string flat_generics = head + '<';
      for (std::size_t index = 0; index < generics.size(); ++index) {
        if (index != 0U) {
          flat_generics += ", ";
        }
        flat_generics += generics[index];
      }
      flat_generics += '>';
      if (flat_generics.size() <= canonical_line_width) {
        head = std::move(flat_generics);
      } else {
        head += "<\n";
        for (std::size_t index = 0; index < generics.size(); ++index) {
          head += "    " + generics[index];
          if (index + 1U != generics.size()) {
            head += ',';
          }
          head += '\n';
        }
        head += "  >";
      }
    }
    std::vector<std::string> inputs;
    inputs.reserve(function.inputs.size());
    for (std::size_t index = 0; index < function.inputs.size(); ++index) {
      const auto& input = function.inputs[index];
      if (index < function.types.bindings.size() &&
          function.types.bindings[index]) {
        inputs.push_back(
            parameter_text(input, *function.types.bindings[index]));
      } else {
        inputs.push_back(parameter_text(input));
      }
    }
    std::string result_text;
    if (!function.results.empty()) {
      result_text = " -> ";
      if (function.results.size() > 1U) {
        result_text += '(';
      }
      for (std::size_t index = 0; index < function.results.size(); ++index) {
        if (index != 0U) {
          result_text += ", ";
        }
        result_text += type_expression_text(function.results[index].domain);
      }
      if (function.results.size() > 1U) {
        result_text += ')';
      }
    }
    const std::string tail = ")" + result_text;
    std::string flat = head + '(';
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += inputs[index];
    }
    flat += tail;
    const bool multiline = flat.size() > 88U;
    if (!multiline) {
      output << flat;
    } else {
      output << head << "(\n";
      for (std::size_t index = 0; index < inputs.size(); ++index) {
        output << "    " << inputs[index];
        if (index + 1U != inputs.size()) {
          output << ',';
        }
        output << '\n';
      }
      if (2U + tail.size() <= canonical_line_width) {
        output << "  " << tail;
      } else {
        output << "  )";
        if (!function.results.empty()) {
          output << " ->\n";
          if (function.results.size() == 1U) {
            output << "    "
                   << type_expression_layout(function.results.front().domain,
                                             4U);
          } else {
            output << "    (\n";
            for (std::size_t index = 0; index < function.results.size();
                 ++index) {
              output << "      "
                     << type_expression_layout(function.results[index].domain,
                                               6U);
              if (index + 1U != function.results.size()) {
                output << ',';
              }
              output << '\n';
            }
            output << "    )";
          }
        }
      }
    }
    if (function.body) {
      output << ' ' << detail::format_function_body(*function.body, 1U);
      continue;
    }
    output << ";\n";
  }
  const bool has_materialized = std::any_of(
      module.storage_->functions.begin(), module.storage_->functions.end(),
      [](const detail::FunctionMember& function) {
        return function.ir != nullptr;
      });
  begin_group(has_materialized);
  std::vector<const detail::FunctionMember*> materialized;
  materialized.reserve(module.storage_->functions.size());
  for (const detail::FunctionMember& function : module.storage_->functions) {
    if (function.ir) {
      materialized.push_back(&function);
    }
  }
  std::sort(materialized.begin(), materialized.end(),
            [](const detail::FunctionMember* left,
               const detail::FunctionMember* right) {
              return left->name < right->name;
            });
  for (const detail::FunctionMember* function : materialized) {
    write_indented(output, joggle::format(*function->ir, function->name));
  }
  output << "}\n";
  return output.str();
}

}  // namespace joggle
