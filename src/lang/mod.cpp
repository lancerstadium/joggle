#include "joggle/mod.h"

#include "sema/domain.h"
#include "lang/expr.h"
#include "lang/prelude.h"
#include "lang/fn.h"
#include "ir/mod.h"
#include "lang/lex.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <vector>

namespace joggle {

namespace {

using Parameter = Mod::ParamDecl;
using ValKind = detail::ValKind;

struct ParsedMod {
  using TypeDefinition = detail::TypeDefinition;
  using FnDef = detail::FnDef;
  using TypeExpr = detail::TypeExpr;
  using GenericDefinition = detail::GenericDefinition;

  std::string name;
  Version version;
  std::vector<Mod::Import> imports;
  std::vector<Loc> import_sources;
  std::vector<TypeDefinition> types;
  std::vector<FnDef> fns;
};

using TokenKind = detail::TokenKind;
using Token = detail::Token;
using Lexer = detail::Lexer;

const Mod::Expr* body_expression(const std::optional<detail::FnBody>& body) {
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

  std::optional<ParsedMod> parse(Diag& output) {
    expect_name("joggle");
    const auto language = integer();
    if (language && *language != 1U) {
      error("unsupported Joggle language version");
    }
    expect(TokenKind::Semicolon, "';'");
    expect_name("mod");
    auto mod_name = name("a mod name");
    expect(TokenKind::At, "'@'");
    auto mod_version = exact_version();
    expect(TokenKind::LeftBrace, "'{'");
    if (mod_name) {
      mod_.name = std::move(*mod_name);
    }
    if (mod_version) {
      mod_.version = *mod_version;
    }

    while (!is(TokenKind::RightBrace) && !is(TokenKind::End) && ok()) {
      if (is_name("import")) {
        const Loc::Pos begin = current_.begin;
        advance();
        parse_import(begin);
      } else if (match_name("type")) {
        parse_type();
      } else if (is_name("fn")) {
        parse_fn();
      } else {
        error("expected import, type, or fn");
      }
    }
    expect(TokenKind::RightBrace, "'}'");
    if (!is(TokenKind::End)) {
      error("unexpected input after mod");
    }
    validate();

    if (!ok()) {
      for (const Issue& diagnostic : diagnostics_.issues()) {
        output.report(diagnostic);
      }
      return std::nullopt;
    }
    return std::move(mod_);
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
    if (match(TokenKind::LeftBracket)) {
      if (!match(TokenKind::RightBracket)) {
        error("expected ']' in subscript operator");
        return std::nullopt;
      }
      return "[]";
    }
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
        const Loc::Pos end = current_.end;
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
    const VersionRange::Kind kind =
        caret                  ? VersionRange::Kind::Caret
        : parsed->size() == 1U ? VersionRange::Kind::Major
        : parsed->size() == 2U ? VersionRange::Kind::Minor
                               : VersionRange::Kind::Exact;
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

  std::optional<ValKind> scalar_parameter_kind() {
    if (match_name("int")) {
      return ValKind::Integer;
    }
    if (match_name("real")) {
      return ValKind::Real;
    }
    if (match_name("bool")) {
      return ValKind::Boolean;
    }
    if (match_name("string")) {
      return ValKind::String;
    }
    if (match_name("type")) {
      return ValKind::Type;
    }
    if (match_name("fn")) {
      return ValKind::Fn;
    }
    if (match_name("bytes")) {
      return ValKind::Bytes;
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

  std::optional<ParsedMod::TypeExpr> literal(ValKind kind) {
    using Expr = ParsedMod::TypeExpr;
    if (kind == ValKind::Boolean) {
      if (match_name("true")) {
        return Expr{Expr::Kind::Boolean, "true", {}};
      }
      if (match_name("false")) {
        return Expr{Expr::Kind::Boolean, "false", {}};
      }
      error("expected true or false");
      return std::nullopt;
    }
    if (kind == ValKind::String) {
      if (!is(TokenKind::String)) {
        error("expected a string literal");
        return std::nullopt;
      }
      std::string value = current_.text;
      advance();
      return Expr{Expr::Kind::String, std::move(value), {}};
    }
    if (kind != ValKind::Integer && kind != ValKind::Real) {
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
    if (kind == ValKind::Integer) {
      std::int64_t value = 0;
      const char* begin = text.data();
      const char* end = begin + text.size();
      const auto parsed = std::from_chars(begin, end, value);
      if (parsed.ec != std::errc{} || parsed.ptr != end) {
        error("integer default is outside int64 range");
        return std::nullopt;
      }
      return Expr{Expr::Kind::Number, std::to_string(value), {}};
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
    return Expr{Expr::Kind::Number, *canonical, {}};
  }

  std::optional<ParsedMod::TypeExpr> literal(detail::Domain domain) {
    using Expr = ParsedMod::TypeExpr;
    if (!domain.list) {
      return literal(domain.element);
    }

    std::vector<Expr> elements;
    expect(TokenKind::LeftBracket, "'['");
    if (!match(TokenKind::RightBracket)) {
      do {
        auto element = literal(domain.element);
        if (element) {
          elements.push_back(std::move(*element));
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightBracket, "']'");
    }
    return Expr{Expr::Kind::List, {}, std::move(elements)};
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
      result.default_value = literal(*domain);
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

  std::vector<ParsedMod::GenericDefinition> fn_generics() {
    std::vector<ParsedMod::GenericDefinition> result;
    if (!match(TokenKind::Less)) {
      return result;
    }
    if (!match(TokenKind::Greater)) {
      do {
        auto generic_name = name("a type variable name");
        std::optional<detail::Domain> domain =
            detail::Domain{ValKind::Type, false};
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

  const ParsedMod::GenericDefinition*
  find_generic(std::span<const ParsedMod::GenericDefinition> generics,
               std::string_view name) const {
    const auto found =
        std::find_if(generics.begin(), generics.end(),
                     [&](const auto& item) { return item.name == name; });
    return found == generics.end() ? nullptr : &*found;
  }

  bool
  is_ir_port(const ParsedMod::TypeExpr& annotation,
             std::span<const ParsedMod::GenericDefinition> generics) const {
    if (detail::compiler_domain(annotation)) {
      return false;
    }
    if (annotation.kind == ParsedMod::TypeExpr::Kind::Variable) {
      const auto* generic = find_generic(generics, annotation.text);
      const auto domain = generic == nullptr
                              ? std::optional<detail::Domain>{}
                              : detail::compiler_domain(generic->domain);
      if (domain && domain->element != ValKind::Type) {
        return false;
      }
    }
    return true;
  }

  ParsedMod::TypeExpr
  type_expression(std::span<const ParsedMod::GenericDefinition> generics,
                  int minimum_precedence = 0) {
    return detail::parse_expression(lexer_, current_, diagnostics_, source_,
                                    generics, minimum_precedence);
  }

  std::vector<ParsedMod::TypeExpr>
  fn_results(std::span<const ParsedMod::GenericDefinition> generics) {
    std::vector<ParsedMod::TypeExpr> result;
    const auto starts_fn_type = [&] {
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
    if (starts_fn_type()) {
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

  void parse_import(Loc::Pos begin) {
    auto import_name = name("an imported mod name");
    expect(TokenKind::At, "'@'");
    auto versions = version_range();
    std::optional<std::string> alias;
    if (match_name("as")) {
      alias = name("an import alias");
    }
    const Loc::Pos end = current_.end;
    expect(TokenKind::Semicolon, "';'");
    if (import_name && versions) {
      std::string stored_alias =
          alias && *alias != *import_name ? std::move(*alias) : std::string{};
      mod_.imports.push_back(
          {std::move(*import_name), *versions, std::move(stored_alias)});
      mod_.import_sources.push_back(Loc{source_, begin, end});
    }
  }

  void parse_type() {
    const Loc::Pos begin = current_.begin;
    auto definition_name = name("a type name");
    auto definition_parameters = parameters(false, true);
    std::vector<Mod::TypeDecl::DerivedParamDecl> derived_parameters;
    if (!match(TokenKind::Semicolon)) {
      expect(TokenKind::LeftBrace, "'{'");
      while (!is(TokenKind::RightBrace) && ok()) {
        auto field = name("a derived parameter name");
        expect(TokenKind::Colon, "':'");
        auto field_domain = parameter_domain();
        expect(TokenKind::Equal, "'='");
        std::vector<ParsedMod::GenericDefinition> variables;
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
      mod_.types.push_back(
          {std::move(*definition_name), std::move(definition_parameters),
           std::move(derived_parameters), Loc{source_, begin, current_.begin}});
    }
  }

  void parse_fn() {
    const Loc::Pos begin = current_.begin;

    std::optional<Mod::FnDecl::Fixity> declared_fixity;
    expect_name("fn");
    if (match_name("prefix")) {
      declared_fixity = Mod::FnDecl::Fixity::Prefix;
    } else if (match_name("infix")) {
      declared_fixity = Mod::FnDecl::Fixity::Infix;
    } else if (match_name("postfix")) {
      declared_fixity = Mod::FnDecl::Fixity::Postfix;
    }
    const bool symbolic = is(TokenKind::LeftParen);
    std::optional<std::string> fn_name;
    if (symbolic) {
      advance();
      fn_name = operator_symbol();
      expect(TokenKind::RightParen, "')'");
    } else {
      fn_name = name("a fn name");
      if (declared_fixity) {
        error("operator fixity requires a symbolic fn name");
      }
    }
    auto generics = fn_generics();
    std::vector<Parameter> inputs;
    std::vector<std::optional<ParsedMod::TypeExpr>> input_bindings;
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
        std::optional<ParsedMod::TypeExpr> binding;
        if (!ir_input && !detail::compiler_domain(input.domain)) {
          const auto* generic =
              input.domain.kind == ParsedMod::TypeExpr::Kind::Variable
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
          const auto domain = detail::compiler_domain(input.domain);
          if (ir_input || !domain || domain->element == ValKind::Fn ||
              domain->element == ValKind::Bytes || input.variadic) {
            error("this parameter cannot have a default value");
          } else {
            input.default_value = literal(*domain);
          }
        }
        inputs.push_back(std::move(input));
        input_bindings.push_back(std::move(binding));
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')'");
    }

    std::vector<ParsedMod::TypeExpr> result_types;
    if (match(TokenKind::Arrow)) {
      result_types = fn_results(generics);
    }
    ParsedMod::FnDef definition;
    if (fn_name) {
      definition.name = std::move(*fn_name);
    }
    definition.generics = generics;
    definition.types.generics = std::move(generics);
    definition.inputs = std::move(inputs);
    definition.types.bindings = std::move(input_bindings);
    for (std::size_t index = 0; index < result_types.size(); ++index) {
      if (!is_ir_port(result_types[index], definition.generics) &&
          !detail::compiler_domain(result_types[index])) {
        const auto* generic =
            result_types[index].kind == ParsedMod::TypeExpr::Kind::Variable
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
      // Fixity belongs to the source call shape. Compile-time arguments are
      // still written operands even though materialization stores them as
      // bindings on the callee rather than SSA edges on the Call.
      const std::size_t operands = definition.inputs.size();
      definition.operator_fixity =
          operands == 1U   ? std::optional{Mod::FnDecl::Fixity::Prefix}
          : operands == 2U ? std::optional{Mod::FnDecl::Fixity::Infix}
                           : std::nullopt;
    }

    if (match(TokenKind::Semicolon)) {
    } else if (is(TokenKind::LeftBrace)) {
      std::vector<ParsedMod::GenericDefinition> variables = definition.generics;
      for (const auto& input : definition.inputs) {
        variables.push_back({input.name, input.domain});
      }
      auto body = detail::parse_fn_body(lexer_, current_, diagnostics_, source_,
                                        variables);
      if (!body) {
        return;
      }
      definition.body = std::move(*body);
    } else {
      error("expected ';' or a fn body");
    }
    if (!definition.name.empty()) {
      definition.source = Loc{source_, begin, current_.begin};
      mod_.fns.push_back(std::move(definition));
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
                           std::string_view owner, std::optional<Loc> source,
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
      std::span<const ParsedMod::GenericDefinition> variables,
      std::string_view owner, std::optional<Loc> source,
      const ParsedMod::TypeExpr& expression, const Mod::Expr& expected) {
    const auto report = [&](std::string message) {
      error(std::move(message), source);
    };
    using Kind = ParsedMod::TypeExpr::Kind;
    const auto domain = detail::compiler_domain(expected);
    if (!domain) {
      report("unknown parameter domain in " + std::string(owner));
      return;
    }
    if (expression.kind == Kind::FnType) {
      const auto signature = detail::callable_type(expression);
      if (domain->list || domain->element != ValKind::Type || !signature) {
        report("malformed fn type in " + std::string(owner));
        return;
      }
      const auto type_domain = detail::domain_expression(ValKind::Type);
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
          detail::domain_expression(ValKind::Boolean));
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
      const auto fixity =
          expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                             : Mod::FnDecl::Fixity::Infix;
      std::vector<const ParsedMod::FnDef*> candidates;
      for (const auto& candidate : mod_.fns) {
        if (candidate.name == expression.text &&
            candidate.operator_fixity == fixity &&
            candidate.inputs.size() == arity &&
            candidate.results.size() == 1U &&
            candidate.results.front().domain == expected) {
          candidates.push_back(&candidate);
        }
      }
      candidates.erase(
          std::remove_if(candidates.begin(), candidates.end(),
                         [&](const ParsedMod::FnDef* candidate) {
                           for (std::size_t index = 0; index < arity; ++index) {
                             const auto& argument = expression.arguments[index];
                             const auto* variable =
                                 argument.kind == Kind::Variable
                                     ? find_generic(variables, argument.text)
                                     : nullptr;
                             if (variable != nullptr &&
                                 candidate->inputs[index].domain !=
                                     variable->domain) {
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
      // after the complete Mod closure is available to Compiler::link.
      return;
    }
    if (expression.kind == Kind::Call) {
      if (!reference_is_visible(expression.text, "fn", source)) {
        return;
      }
      // Calls need the complete import closure, overload set, labels, and
      // defaults. Compiler::link performs that semantic check once Mods
      // have immutable declarations; parsing only checks visibility here.
      return;
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      const bool matches = (expression.kind == Kind::Number &&
                            (domain->element == ValKind::Integer ||
                             domain->element == ValKind::Real)) ||
                           (expression.kind == Kind::Boolean &&
                            domain->element == ValKind::Boolean) ||
                           (expression.kind == Kind::String &&
                            domain->element == ValKind::String);
      if (!matches) {
        report("literal has the wrong domain in " + std::string(owner));
      }
      return;
    }
    if (!reference_is_visible(expression.text, "type", source)) {
      return;
    }
    if (domain->element != ValKind::Type) {
      report("type expression reference has the wrong domain in " +
             std::string(owner));
      return;
    }
    const std::size_t dot = expression.text.find('.');
    const bool local =
        dot == std::string::npos || expression.text.substr(0, dot) == mod_.name;
    if (!local) {
      return;
    }
    const std::string_view name =
        dot == std::string::npos
            ? std::string_view(expression.text)
            : std::string_view(expression.text).substr(dot + 1U);
    const auto declaration = std::find_if(
        mod_.types.begin(), mod_.types.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    if (declaration == mod_.types.end()) {
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
                            std::optional<Loc> source = std::nullopt) {
    const std::size_t dot = reference.find('.');
    if (dot == std::string_view::npos) {
      return true;
    }
    const std::string_view mod_name = reference.substr(0, dot);
    if (mod_name == detail::prelude_mod_name) {
      return true;
    }
    const bool imported = std::any_of(
        mod_.imports.begin(), mod_.imports.end(),
        [&](const Mod::Import& import) { return import.prefix() == mod_name; });
    if (mod_name != mod_.name && !imported) {
      error(std::string(kind) + " reference '" + std::string(reference) +
                "' belongs to a mod that is not imported",
            source);
      return false;
    }
    return true;
  }

  void validate() {
    std::unordered_set<std::string_view> import_names;
    std::unordered_set<std::string_view> import_prefixes;
    for (std::size_t index = 0; index < mod_.imports.size(); ++index) {
      const Mod::Import& import = mod_.imports[index];
      const Loc& source = mod_.import_sources[index];
      if (!import_names.insert(import.name).second) {
        error("duplicate import '" + import.name + "'", source);
      }
      if (import.prefix() == mod_.name) {
        error("import prefix '" + std::string(import.prefix()) +
                  "' conflicts with the mod name",
              source);
      } else if (!import_prefixes.insert(import.prefix()).second) {
        error("duplicate import prefix '" + std::string(import.prefix()) + "'",
              source);
      }
    }
    check_unique(mod_.types, "type");
    for (std::size_t left = 0; left < mod_.fns.size(); ++left) {
      const auto& fn = mod_.fns[left];
      for (std::size_t right = left + 1U; right < mod_.fns.size(); ++right) {
        const auto& candidate = mod_.fns[right];
        if (fn.name != candidate.name ||
            fn.generics.size() != candidate.generics.size() ||
            fn.inputs.size() != candidate.inputs.size() ||
            fn.results.size() != candidate.results.size()) {
          continue;
        }
        const bool same_inputs = std::equal(
            fn.inputs.begin(), fn.inputs.end(), candidate.inputs.begin(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.variadic == rhs.variadic && lhs.domain == rhs.domain;
            });
        const bool same_results = std::equal(
            fn.results.begin(), fn.results.end(), candidate.results.begin(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.domain == rhs.domain;
            });
        if (same_inputs && same_results) {
          error("duplicate fn overload '" + fn.name + "'", candidate.source);
        }
      }
    }
    for (const auto& type : mod_.types) {
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
    for (const auto& fn : mod_.fns) {
      validate_parameters(fn.inputs, fn.name, fn.source, true);
      validate_parameters(fn.results, fn.name, fn.source, true);
      std::unordered_set<std::string> input_names;
      for (std::size_t index = 0; index < fn.inputs.size(); ++index) {
        const auto& input = fn.inputs[index];
        if (!input_names.insert(input.name).second) {
          error("duplicate fn input '" + input.name + "' in '" + fn.name + "'",
                fn.source);
        }
        if (input.variadic && index + 1U != fn.inputs.size()) {
          error("variadic input must be last in '" + fn.name + "'", fn.source);
        }
      }
      std::unordered_set<std::string> generics;
      for (const auto& generic : fn.generics) {
        if (!generics.insert(generic.name).second) {
          error("duplicate type variable '" + generic.name + "' in '" +
                    fn.name + "'",
                fn.source);
        }
      }
      const auto type_domain = detail::domain_expression(ValKind::Type);
      const std::string owner = "fn '" + fn.name + "'";
      for (std::size_t index = 0; index < fn.inputs.size(); ++index) {
        const auto& input = fn.inputs[index];
        if (detail::is_value_port(input)) {
          validate_declaration_expression(fn.generics, owner, fn.source,
                                          input.domain, type_domain);
        }
      }
      if (!fn.types.bindings.empty()) {
        if (fn.types.bindings.size() != fn.inputs.size()) {
          error("fn '" + fn.name + "' has an invalid static binding contract",
                fn.source);
        } else {
          for (std::size_t index = 0; index < fn.inputs.size(); ++index) {
            if (fn.types.bindings[index]) {
              validate_declaration_expression(fn.generics, owner, fn.source,
                                              *fn.types.bindings[index],
                                              fn.inputs[index].domain);
            }
          }
        }
      }
      for (std::size_t index = 0; index < fn.results.size(); ++index) {
        const auto& result = fn.results[index];
        const auto expected =
            detail::is_value_port(result) ? type_domain : result.domain;
        if (detail::is_value_port(result)) {
          validate_declaration_expression(fn.generics, owner, fn.source,
                                          result.domain, expected);
        }
      }
      if (fn.operator_fixity) {
        const auto mod_values = static_cast<std::size_t>(std::count_if(
            fn.inputs.begin(), fn.inputs.end(), detail::is_value_port));
        const std::size_t operands = fn.inputs.size();
        const std::size_t required =
            fn.operator_fixity == Mod::FnDecl::Fixity::Infix ? 2U : 1U;
        const bool variadic_subscript =
            fn.operator_fixity == Mod::FnDecl::Fixity::Infix &&
            fn.name == "[]" && operands >= 2U && !fn.inputs.empty() &&
            fn.inputs.back().variadic;
        const bool expected_ir_result = mod_values != 0U;
        if ((!variadic_subscript && operands != required) ||
            fn.results.size() != 1U ||
            detail::is_value_port(fn.results.front()) != expected_ir_result) {
          error("operator on fn '" + fn.name +
                    "' has an incompatible signature",
                fn.source);
        }
      }
      if (const Mod::Expr* expression = body_expression(fn.body);
          expression != nullptr && fn.results.size() == 1U &&
          !detail::is_value_port(fn.results.front()) &&
          std::none_of(fn.inputs.begin(), fn.inputs.end(),
                       detail::is_value_port)) {
        std::vector<ParsedMod::GenericDefinition> variables = fn.generics;
        for (std::size_t index = 0; index < fn.inputs.size(); ++index) {
          const auto& input = fn.inputs[index];
          if (!detail::is_value_port(input)) {
            variables.push_back({input.name, input.domain});
          }
        }
        validate_declaration_expression(variables, owner, fn.source,
                                        *expression, fn.results.front().domain);
      }
    }
  }

  void error(std::string message) {
    diagnostics_.report(std::move(message),
                        Loc{source_, current_.begin, current_.end});
  }

  void error(std::string message, Loc source) {
    diagnostics_.report(std::move(message), std::move(source));
  }

  void error(std::string message, std::optional<Loc> source) {
    if (source) {
      error(std::move(message), std::move(*source));
    } else {
      error(std::move(message));
    }
  }

  Lexer lexer_;
  std::string source_;
  Token current_;
  ParsedMod mod_;
  Diag diagnostics_;
};

}  // namespace

std::optional<Mod> parse_mod(std::string_view text, Diag& diagnostics,
                             std::string source) {
  auto parsed = Parser(text, std::move(source)).parse(diagnostics);
  if (!parsed) {
    return std::nullopt;
  }
  auto storage = std::make_shared<Mod::Storage>();
  storage->name = std::move(parsed->name);
  storage->version = parsed->version;
  storage->imports = std::move(parsed->imports);
  storage->import_sources = std::move(parsed->import_sources);
  storage->types = std::move(parsed->types);
  storage->fns.reserve(parsed->fns.size());
  for (detail::FnDef& fn : parsed->fns) {
    std::string name = fn.name;
    storage->fns.push_back({std::move(name), std::move(fn), nullptr});
  }
  Mod mod(storage);
  storage->digest = Mod::compute_digest(storage);
  storage->declaration_digest = Mod::compute_declaration_digest(storage);
  return mod;
}

}  // namespace joggle
