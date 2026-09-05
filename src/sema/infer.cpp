#include "sema/infer.h"

#include "sema/call.h"
#include "compile/compiler.h"
#include "sema/domain.h"
#include "lang/expr.h"
#include "ir/mod.h"
#include "lang/prelude.h"
#include "ir/type.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_map>

namespace joggle::detail {
namespace {

using Bindings = KnownBindings;

std::optional<Mod::Expr> value_domain(const ParamVal& value) {
  switch (value.kind()) {
  case ParamVal::Kind::I64:
    return domain_expression(ValKind::Integer);
  case ParamVal::Kind::F64:
    return domain_expression(ValKind::Real);
  case ParamVal::Kind::Boolean:
    return domain_expression(ValKind::Boolean);
  case ParamVal::Kind::String:
    return domain_expression(ValKind::String);
  case ParamVal::Kind::Type:
    return domain_expression(ValKind::Type);
  case ParamVal::Kind::List: {
    const auto elements = value.elements();
    if (elements.empty()) {
      return std::nullopt;
    }
    auto element = value_domain(elements.front());
    if (!element) {
      return std::nullopt;
    }
    for (std::size_t index = 1U; index < elements.size(); ++index) {
      if (value_domain(elements[index]) != element) {
        return std::nullopt;
      }
    }
    return Mod::Expr::list_domain(std::move(*element));
  }
  }
  return std::nullopt;
}

std::optional<Mod::Expr> known_domain(const Mod::Expr& expression,
                                      const Bindings& bindings) {
  using Kind = Mod::Expr::Kind;
  if (expression.kind == Kind::Variable ||
      (expression.kind == Kind::Reference && expression.arguments.empty())) {
    const auto value = bindings.find(expression.text);
    if (value == bindings.end()) {
      return std::nullopt;
    }
    if (value->second.domain) {
      return value->second.domain;
    }
    return value_domain(value->second.value);
  }
  if (expression.kind == Kind::Number) {
    return domain_expression(expression.text.find_first_of(".eE") ==
                                     std::string::npos
                                 ? ValKind::Integer
                                 : ValKind::Real);
  }
  if (expression.kind == Kind::Boolean) {
    return domain_expression(ValKind::Boolean);
  }
  if (expression.kind == Kind::String) {
    return domain_expression(ValKind::String);
  }
  if (expression.kind == Kind::List && !expression.arguments.empty()) {
    auto element = known_domain(expression.arguments.front(), bindings);
    const auto domain = element ? compiler_domain(*element) : std::nullopt;
    if (!domain || domain->list) {
      return std::nullopt;
    }
    for (std::size_t index = 1U; index < expression.arguments.size(); ++index) {
      if (known_domain(expression.arguments[index], bindings) != element) {
        return std::nullopt;
      }
    }
    return Mod::Expr::list_domain(std::move(*element));
  }
  return std::nullopt;
}

struct Environment {
  using Evaluator = std::function<std::optional<ParamVal>(
      Mod::FnDecl, std::span<const ParamVal>)>;
  using EvaluationCheck = std::function<bool(const Mod::FnDecl&)>;
  using FnLookup = std::function<std::vector<Mod::FnDecl>(std::string_view,
                                                          std::string_view)>;
  using OperatorLookup = std::function<std::vector<Mod::FnDecl>(
      std::string_view, std::string_view, Mod::FnDecl::Fixity)>;
  std::function<std::optional<Mod>(std::string_view)> mod;
  std::function<std::optional<Type>(const Mod::TypeDecl&,
                                    std::span<const ParamVal>)>
      type;
  FnLookup fns;
  OperatorLookup operators;
  EvaluationCheck can_evaluate;
  Evaluator evaluate;
  bool require_hermetic_host_evaluation = false;
  Compiler::Limits limits;
};

Environment environment(Compiler& compiler, bool allow_host_evaluation = true) {
  return {
      [&](std::string_view name) { return compiler.mod(name); },
      [&](const Mod::TypeDecl& schema, std::span<const ParamVal> parameters) {
        return CompilerAccess::make(compiler, schema,
                                    std::span<const ParamVal>(parameters));
      },
      [&](std::string_view owner, std::string_view reference) {
        return visible_fns(compiler, owner, reference);
      },
      [&](std::string_view owner, std::string_view symbol,
          Mod::FnDecl::Fixity fixity) {
        return visible_operators(compiler, owner, symbol, fixity);
      },
      [&,
       under_residual_control = !allow_host_evaluation](const Mod::FnDecl& fn) {
        return CompilerAccess::can_evaluate(compiler, fn,
                                            under_residual_control);
      },
      Environment::Evaluator{
          [&, under_residual_control = !allow_host_evaluation](
              Mod::FnDecl fn, std::span<const ParamVal> arguments) {
            return CompilerAccess::evaluate(compiler, fn, arguments,
                                            under_residual_control);
          }},
      !allow_host_evaluation,
      CompilerAccess::limits(compiler)};
}

Environment environment(std::span<const Mod> mods, Diag& diagnostics) {
  const auto find = [mods](std::string_view name) -> std::optional<Mod> {
    const auto mod =
        std::find_if(mods.begin(), mods.end(),
                     [&](const Mod& value) { return value.name() == name; });
    return mod == mods.end() ? std::nullopt : std::optional<Mod>{*mod};
  };
  return {find,
          [mods, &diagnostics](
              const Mod::TypeDecl& schema,
              std::span<const ParamVal> parameters) -> std::optional<Type> {
            auto values = validate_parameters(schema.symbol().qualified_name(),
                                              schema.parameters(), parameters,
                                              diagnostics);
            if (!values) {
              return std::nullopt;
            }
            auto derived =
                resolve_derived_parameters(mods, schema, *values, diagnostics);
            return derived
                       ? std::optional<Type>{TypeAccess::make(
                             schema, std::move(*values), std::move(*derived))}
                       : std::nullopt;
          },
          [mods](std::string_view owner, std::string_view reference) {
            return visible_fns(mods, owner, reference);
          },
          [mods](std::string_view owner, std::string_view symbol,
                 Mod::FnDecl::Fixity fixity) {
            return visible_operators(mods, owner, symbol, fixity);
          },
          [](const Mod::FnDecl& fn) { return is_prelude_primitive(fn); },
          [&diagnostics](Mod::FnDecl fn, std::span<const ParamVal> arguments) {
            const Compiler::Limits limits;
            return evaluate_prelude_primitive(fn, arguments, diagnostics,
                                              limits.steps);
          },
          false,
          {}};
}

class Solver {
public:
  Solver(Environment environment, const Mod::FnDecl& schema, Diag& diagnostics,
         std::optional<Loc> source)
      : limits_(environment.limits), environment_(std::move(environment)),
        schema_(&schema), diagnostics_(diagnostics), source_(std::move(source)),
        contract_(&FnTypeAccess::get(schema)),
        scope_(schema.symbol().mod_name()) {}

  Solver(Environment environment, const Mod::TypeDecl& schema,
         Diag& diagnostics)
      : limits_(environment.limits), environment_(std::move(environment)),
        diagnostics_(diagnostics), scope_(schema.symbol().mod_name()) {}

  Solver(Environment environment, std::string scope, Diag& diagnostics,
         std::optional<Loc> source)
      : limits_(environment.limits), environment_(std::move(environment)),
        diagnostics_(diagnostics), source_(std::move(source)),
        scope_(std::move(scope)) {}

  std::optional<ParamVal> evaluate_known(const Mod::Expr& expression,
                                         const Mod::ParamDecl& expected,
                                         const Bindings& bindings) {
    return evaluate(expression, expected, bindings);
  }

  std::optional<CallTypes>
  infer(std::span<const Type> arguments,
        std::span<const std::optional<ParamVal>> known_arguments,
        std::span<const std::optional<Type>> expected) {
    std::vector<std::optional<Type>> values;
    values.reserve(arguments.size());
    for (const Type& argument : arguments) {
      values.emplace_back(argument);
    }
    return infer_partial(values, known_arguments, expected);
  }

  std::optional<CallTypes>
  infer_partial(std::span<const std::optional<Type>> arguments,
                std::span<const std::optional<ParamVal>> known_arguments,
                std::span<const std::optional<Type>> expected) {
    if (schema_ == nullptr || contract_ == nullptr) {
      report("call solver has no fn declaration");
      return std::nullopt;
    }
    const auto compiler_ports = compiler_inputs(*schema_);
    const auto input_ports = value_inputs(*schema_);
    const auto result_ports = value_results(*schema_);
    if (known_arguments.size() != compiler_ports.size()) {
      report("call Known-argument map does not match its fn");
      return std::nullopt;
    }
    Bindings bindings;
    // Direct Known bindings establish dependent variables before Residual
    // callable Types are matched. For example, a shape argument can determine
    // the arity of a following lambda. Computed Known patterns still run in
    // the complete pass below after Residual Types have contributed bindings.
    std::size_t early_known_index = 0;
    for (std::size_t input_index = 0; input_index < schema_->inputs().size();
         ++input_index) {
      const auto& input = schema_->inputs()[input_index];
      if (is_value_port(input)) {
        continue;
      }
      std::optional<ParamVal> actual = known_arguments[early_known_index++];
      if (!actual && input.default_value) {
        actual = parameter_default(input);
      }
      const Mod::Expr* pattern =
          contract_->bindings.empty() || !contract_->bindings[input_index]
              ? nullptr
              : &*contract_->bindings[input_index];
      if (actual && pattern && pattern->kind == Mod::Expr::Kind::Variable &&
          !unify(*pattern, *actual, bindings)) {
        return std::nullopt;
      }
    }
    std::size_t argument = 0;
    for (const auto& input : input_ports) {
      const std::size_t count =
          input.variadic ? arguments.size() - argument : 1U;
      if (argument + count > arguments.size()) {
        report("fn call has too few arguments");
        return std::nullopt;
      }
      for (std::size_t item = 0; item < count; ++item) {
        const auto& actual = arguments[argument++];
        if (actual && !unify(input.domain, ParamVal(*actual), bindings)) {
          return std::nullopt;
        }
      }
    }
    if (argument != arguments.size()) {
      report("fn call has too many arguments");
      return std::nullopt;
    }
    std::size_t known_index = 0;
    for (std::size_t input_index = 0; input_index < schema_->inputs().size();
         ++input_index) {
      const auto& input = schema_->inputs()[input_index];
      if (is_value_port(input)) {
        continue;
      }
      std::optional<ParamVal> actual = known_arguments[known_index++];
      if (!actual && input.default_value) {
        actual = parameter_default(input);
      }
      if (!actual) {
        if (!contract_->bindings.empty() && contract_->bindings[input_index]) {
          report("fn call is missing Known argument '" + input.name + "'");
          return std::nullopt;
        }
        continue;
      }
      if (!matches_parameter(input, *actual)) {
        report("fn call Known argument '" + input.name +
               "' has the wrong domain");
        return std::nullopt;
      }
      if (!contract_->bindings.empty() && contract_->bindings[input_index] &&
          !unify(*contract_->bindings[input_index], *actual, bindings)) {
        return std::nullopt;
      }
    }
    if (expected.size() != result_ports.size()) {
      report("fn call result count does not match its declaration");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (expected[index] && !unify(result_ports[index].domain,
                                    ParamVal(*expected[index]), bindings)) {
        return std::nullopt;
      }
    }

    const Mod::ParamDecl type_parameter{
        "result", domain_expression(ValKind::Type), false, std::nullopt};
    CallTypes resolved;
    resolved.arguments.reserve(arguments.size());
    argument = 0;
    for (const auto& input : input_ports) {
      const std::size_t count =
          input.variadic ? arguments.size() - argument : 1U;
      for (std::size_t item = 0; item < count; ++item) {
        if (arguments[argument]) {
          resolved.arguments.push_back(*arguments[argument++]);
          continue;
        }
        auto value = evaluate(input.domain, type_parameter, bindings);
        if (!value || value->as_type() == nullptr) {
          report("while resolving input '" + input.name + "'");
          return std::nullopt;
        }
        resolved.arguments.push_back(*value->as_type());
        ++argument;
      }
    }
    resolved.results.reserve(result_ports.size());
    for (const auto& result : result_ports) {
      auto value = evaluate(result.domain, type_parameter, bindings);
      if (!value || value->as_type() == nullptr) {
        report("while resolving result '" + result.name + "'");
        return std::nullopt;
      }
      resolved.results.push_back(*value->as_type());
    }
    resolved.bindings = std::move(bindings);
    return resolved;
  }

  std::optional<std::vector<ParamVal>>
  derive(const Mod::TypeDecl& schema, std::span<const ParamVal> parameters) {
    if (parameters.size() != schema.parameters().size()) {
      report("type instance has an invalid parameter binding");
      return std::nullopt;
    }
    Bindings bindings;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      bindings.emplace(
          schema.parameters()[index].name,
          KnownBinding{parameters[index], schema.parameters()[index].domain});
    }
    std::vector<ParamVal> values;
    values.reserve(schema.derived_parameters().size());
    for (const auto& derived : schema.derived_parameters()) {
      const Mod::ParamDecl field{derived.name, derived.domain, false,
                                 std::nullopt};
      auto value = evaluate(derived.value, field, bindings);
      if (!value) {
        return std::nullopt;
      }
      values.push_back(std::move(*value));
    }
    return values;
  }

private:
  void report(std::string message) {
    diagnostics_.report(std::move(message), source_);
  }

  template <typename Declaration>
  std::optional<Declaration> declaration(std::string_view reference) {
    const std::size_t dot = reference.find('.');
    std::string_view owner = scope_;
    if (dot != std::string_view::npos) {
      const std::string_view prefix = reference.substr(0, dot);
      owner = prefix;
      const auto scope = environment_.mod(scope_);
      if (scope) {
        const auto imported =
            std::find_if(scope->imports().begin(), scope->imports().end(),
                         [&](const Mod::Import& import) {
                           return import.prefix() == prefix;
                         });
        if (imported != scope->imports().end()) {
          owner = imported->name;
        }
      }
    }
    const std::string_view local =
        dot == std::string_view::npos ? reference : reference.substr(dot + 1U);
    const auto mod = environment_.mod(owner);
    if (!mod) {
      report("type contract references unknown mod '" + std::string(owner) +
             "'");
      return std::nullopt;
    }
    std::optional<Declaration> result;
    if constexpr (std::is_same_v<Declaration, Mod::TypeDecl>) {
      result = mod->type(local);
    } else {
      static_assert(std::is_same_v<Declaration, Mod::FnDecl>);
      result = mod->fn(local);
    }
    if (result && owner != scope_ && !result->exported()) {
      report("type contract references private declaration '" +
             std::string(reference) + "'");
      return std::nullopt;
    }
    if (!result) {
      report("type contract references unknown declaration '" +
             std::string(reference) + "'");
    }
    return result;
  }

  std::optional<ParamVal> expression_literal(const TypeExpr& expression,
                                             ValKind expected) {
    using Kind = TypeExpr::Kind;
    if (expected == ValKind::Integer && expression.kind == Kind::Number) {
      std::int64_t value = 0;
      const auto parsed = std::from_chars(
          expression.text.data(),
          expression.text.data() + expression.text.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == expression.text.data() + expression.text.size()) {
        return ParamVal(value);
      }
    } else if (expected == ValKind::Real && expression.kind == Kind::Number) {
      double value = 0.0;
      std::istringstream input(expression.text);
      input.imbue(std::locale::classic());
      input >> value;
      if (input && input.peek() == std::char_traits<char>::eof()) {
        return ParamVal(value);
      }
    } else if (expected == ValKind::Boolean &&
               expression.kind == Kind::Boolean) {
      return ParamVal(expression.text == "true");
    } else if (expected == ValKind::String && expression.kind == Kind::String) {
      return ParamVal(expression.text);
    }
    report("type expression literal has the wrong kind");
    return std::nullopt;
  }

  std::optional<ParamVal> evaluate_derived_parameter(
      const GenericDefinition& generic, std::string_view field_name,
      const Mod::ParamDecl& expected, const Bindings& bindings) {
    const auto bound = bindings.find(generic.name);
    const Type* type =
        bound == bindings.end() ? nullptr : bound->second.value.as_type();
    if (type == nullptr) {
      report("cannot evaluate derived parameter '" + generic.name + "." +
             std::string(field_name) +
             "' before its receiver type is inferred");
      return std::nullopt;
    }
    const auto derived = std::find_if(
        type->schema().derived_parameters().begin(),
        type->schema().derived_parameters().end(), [&](const auto& candidate) {
          return candidate.name == field_name &&
                 candidate.domain == expected.domain;
        });
    if (derived == type->schema().derived_parameters().end()) {
      const auto parameter = std::find_if(
          type->schema().parameters().begin(),
          type->schema().parameters().end(), [&](const auto& candidate) {
            return candidate.name == field_name &&
                   candidate.domain == expected.domain;
          });
      if (parameter != type->schema().parameters().end()) {
        const auto index = static_cast<std::size_t>(
            std::distance(type->schema().parameters().begin(), parameter));
        const auto values = TypeAccess::parameters(*type);
        return index < values.size() ? std::optional<ParamVal>{values[index]}
                                     : std::nullopt;
      }
      report("type '" + type->schema().symbol().qualified_name() +
             "' has no derived parameter '" + std::string(field_name) + "'");
      return std::nullopt;
    }

    Bindings arguments;
    const auto parameters = TypeAccess::parameters(*type);
    if (parameters.size() != type->schema().parameters().size()) {
      report("type instance has an invalid parameter binding");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      arguments.emplace(
          type->schema().parameters()[index].name,
          KnownBinding{parameters[index],
                       type->schema().parameters()[index].domain});
    }
    const std::string identity = std::string(type->stable_name()) +
                                 "/derived/" + std::string(field_name);
    if (std::find(calls_.begin(), calls_.end(), identity) != calls_.end()) {
      report("recursive derived parameter '" + generic.name + "." +
             std::string(field_name) + "'");
      return std::nullopt;
    }
    calls_.push_back(identity);
    const std::string caller_scope = scope_;
    scope_ = std::string(type->schema().symbol().mod_name());
    const Mod::ParamDecl field{derived->name, derived->domain, false,
                               std::nullopt};
    auto value = evaluate(derived->value, field, arguments);
    scope_ = caller_scope;
    calls_.pop_back();
    return value;
  }

  std::vector<Mod::FnDecl> operator_declarations(std::string_view symbol,
                                                 Mod::FnDecl::Fixity fixity,
                                                 const Mod::ParamDecl& expected,
                                                 std::size_t arity) {
    std::vector<Mod::FnDecl> result;
    for (const auto& candidate :
         environment_.operators(scope_, symbol, fixity)) {
      const auto known_inputs = compiler_inputs(candidate);
      const auto known_results = compiler_results(candidate);
      if (!value_inputs(candidate).empty() ||
          !value_results(candidate).empty() || known_inputs.size() != arity ||
          known_results.size() != 1U ||
          known_results.front().domain != expected.domain) {
        continue;
      }
      result.push_back(candidate);
    }
    return result;
  }

  std::optional<ParamVal> evaluate_fn(const Mod::FnDecl& fn,
                                      std::span<const ParamVal> values,
                                      const Bindings& arguments) {
    const std::string identity = fn.symbol().stable_name();
    if (std::find(calls_.begin(), calls_.end(), identity) != calls_.end()) {
      report("recursive compile-time call to '" + fn.symbol().qualified_name() +
             "'");
      return std::nullopt;
    }
    calls_.push_back(identity);
    struct CallGuard {
      std::vector<std::string>& calls;
      ~CallGuard() { calls.pop_back(); }
    } guard{calls_};

    if (environment_.require_hermetic_host_evaluation &&
        (!environment_.can_evaluate || !environment_.can_evaluate(fn))) {
      report("host implementation of fn '" + fn.symbol().qualified_name() +
             "' is guarded and cannot execute under Residual control");
      return std::nullopt;
    }
    if (environment_.evaluate &&
        (!environment_.can_evaluate || environment_.can_evaluate(fn))) {
      return environment_.evaluate(fn, values);
    }

    const Mod::Expr* body = ModAccess::expression(fn);
    const auto results = compiler_results(fn);
    if (body == nullptr || results.size() != 1U) {
      report("compile-time fn '" + fn.symbol().qualified_name() +
             "' has no available evaluator");
      return std::nullopt;
    }
    struct ScopeGuard {
      std::string& scope;
      std::string caller;
      ~ScopeGuard() { scope = std::move(caller); }
    } scope_guard{scope_, scope_};
    scope_ = std::string(fn.symbol().mod_name());
    return evaluate(*body, results.front(), arguments);
  }

  std::optional<ParamVal> evaluate(const TypeExpr& expression,
                                   const Mod::ParamDecl& expected,
                                   const Bindings& bindings) {
    if (steps_ >= limits_.steps) {
      if (!budget_reported_) {
        report("compile-time evaluation step limit exceeded");
        budget_reported_ = true;
      }
      return std::nullopt;
    }
    ++steps_;
    if (depth_ >= limits_.depth) {
      if (!budget_reported_) {
        report("compile-time evaluation depth limit exceeded");
        budget_reported_ = true;
      }
      return std::nullopt;
    }
    ++depth_;
    struct DepthGuard {
      std::size_t& depth;
      ~DepthGuard() { --depth; }
    } guard{depth_};
    using Kind = TypeExpr::Kind;
    const auto domain = compiler_domain(expected.domain);
    if (!domain) {
      report("unknown parameter domain for '" + expected.name + "'");
      return std::nullopt;
    }
    if (expression.kind == Kind::FnType) {
      const auto signature = callable_type(expression);
      if (domain->list || domain->element != ValKind::Type || !signature) {
        report("malformed fn type expression");
        return std::nullopt;
      }
      const Mod::ParamDecl type_element{"signature element",
                                        domain_expression(ValKind::Type), false,
                                        std::nullopt};
      std::vector<ParamVal> parameters;
      parameters.reserve(2U);
      for (const auto side : {signature->inputs, signature->results}) {
        std::vector<ParamVal> types;
        types.reserve(side.size());
        for (const auto& element : side) {
          auto type = evaluate(element, type_element, bindings);
          if (!type || type->as_type() == nullptr) {
            return std::nullopt;
          }
          types.push_back(std::move(*type));
        }
        parameters.push_back(ParamVal::list(std::move(types)));
      }
      auto callable = declaration<Mod::TypeDecl>("prelude.callable");
      auto value = callable ? environment_.type(*callable, parameters)
                            : std::optional<Type>{};
      return value ? std::optional<ParamVal>{ParamVal(*value)} : std::nullopt;
    }
    if (expression.kind == Kind::Variable) {
      const auto found = bindings.find(expression.text);
      if (found == bindings.end()) {
        report("cannot infer type variable '" + expression.text + "'");
        return std::nullopt;
      }
      return found->second.value;
    }
    if (expression.kind == Kind::Evaluate) {
      if (expression.arguments.size() != 1U) {
        report("malformed compile-time evaluation expression");
        return std::nullopt;
      }
      return evaluate(expression.arguments.front(), expected, bindings);
    }
    if (expression.kind == Kind::If) {
      if (expression.arguments.size() != 3U) {
        report("malformed compile-time if expression");
        return std::nullopt;
      }
      const Mod::ParamDecl condition{"condition",
                                     domain_expression(ValKind::Boolean), false,
                                     std::nullopt};
      auto selected = evaluate(expression.arguments[0], condition, bindings);
      const bool* value = selected ? selected->as_bool() : nullptr;
      if (value == nullptr) {
        report("compile-time if condition is not known as bool");
        return std::nullopt;
      }
      return evaluate(expression.arguments[*value ? 1U : 2U], expected,
                      bindings);
    }
    if (expression.kind == Kind::Reference) {
      const std::size_t field_dot = expression.text.find('.');
      if (field_dot != std::string::npos) {
        const std::string_view receiver(expression.text.data(), field_dot);
        const auto generic = std::find_if(
            contract_ ? contract_->generics.begin() : empty_generics_.begin(),
            contract_ ? contract_->generics.end() : empty_generics_.end(),
            [&](const auto& candidate) { return candidate.name == receiver; });
        const auto generic_end =
            contract_ ? contract_->generics.end() : empty_generics_.end();
        if (generic != generic_end) {
          return evaluate_derived_parameter(
              *generic,
              std::string_view(expression.text).substr(field_dot + 1U),
              expected, bindings);
        }
      }
    }
    if (domain->list && expression.kind != Kind::Call) {
      if (expression.kind != Kind::List) {
        report("expected a list-valued type expression");
        return std::nullopt;
      }
      Mod::ParamDecl element = expected;
      element.domain = domain_expression(domain->element);
      std::vector<ParamVal> values;
      for (const auto& argument : expression.arguments) {
        auto value = evaluate(argument, element, bindings);
        if (!value) {
          return std::nullopt;
        }
        values.push_back(std::move(*value));
      }
      return ParamVal::list(std::move(values));
    }
    const bool operator_expression = expression.kind == Kind::Prefix ||
                                     expression.kind == Kind::Infix ||
                                     expression.kind == Kind::Postfix;
    if (operator_expression) {
      const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
      if (expression.arguments.size() != arity) {
        report("malformed operator expression");
        return std::nullopt;
      }
      const auto fixity =
          expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                             : Mod::FnDecl::Fixity::Infix;
      auto overloads =
          operator_declarations(expression.text, fixity, expected, arity);
      overloads.erase(
          std::remove_if(overloads.begin(), overloads.end(),
                         [&](const Mod::FnDecl& candidate) {
                           const auto inputs = compiler_inputs(candidate);
                           for (std::size_t index = 0; index < arity; ++index) {
                             const auto actual = known_domain(
                                 expression.arguments[index], bindings);
                             if (actual && inputs[index].domain != *actual) {
                               return true;
                             }
                           }
                           return false;
                         }),
          overloads.end());
      if (overloads.size() > 1U) {
        report("compile-time operator '" + expression.text + "' is ambiguous");
        return std::nullopt;
      }
      if (overloads.size() == 1U) {
        const auto fn = overloads.front();
        Bindings arguments;
        std::vector<ParamVal> values;
        values.reserve(arity);
        const auto inputs = compiler_inputs(fn);
        for (std::size_t index = 0; index < arity; ++index) {
          auto value =
              evaluate(expression.arguments[index], inputs[index], bindings);
          if (!value) {
            return std::nullopt;
          }
          values.push_back(*value);
          arguments.emplace(
              inputs[index].name,
              KnownBinding{std::move(*value), inputs[index].domain});
        }
        return evaluate_fn(fn, values, arguments);
      }
      report("no matching compile-time operator '" + expression.text + "'");
      return std::nullopt;
    }
    if (expression.kind == Kind::Call) {
      std::vector<CallCandidate> candidates;
      for (const auto& fn : environment_.fns(scope_, expression.text)) {
        auto candidate = call_candidate(fn, expression);
        const auto results = compiler_results(fn);
        if (!candidate || !value_inputs(fn).empty() ||
            !value_results(fn).empty() || results.size() != 1U ||
            results.front().domain != expected.domain) {
          continue;
        }
        bool accepts_known_domains = true;
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          const auto actual =
              known_domain(expression.arguments[index], bindings);
          if (actual &&
              fn.inputs()[candidate->parameters[index]].domain != *actual) {
            accepts_known_domains = false;
            break;
          }
        }
        if (accepts_known_domains) {
          candidates.push_back(std::move(*candidate));
        }
      }
      if (candidates.empty()) {
        report("no compile-time overload of '" + expression.text +
               "' accepts this call");
        return std::nullopt;
      }

      std::vector<ParamVal> supplied;
      supplied.reserve(expression.arguments.size());
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        const auto& first = candidates.front();
        const auto& first_parameter =
            first.fn.inputs()[first.parameters[index]];
        const bool common = std::all_of(
            candidates.begin() + 1, candidates.end(),
            [&](const CallCandidate& current) {
              return current.fn.inputs()[current.parameters[index]].domain ==
                     first_parameter.domain;
            });
        if (!common) {
          report("compile-time call to '" + expression.text +
                 "' is ambiguous before argument evaluation");
          return std::nullopt;
        }
        auto value =
            evaluate(expression.arguments[index], first_parameter, bindings);
        if (!value) {
          return std::nullopt;
        }
        supplied.push_back(std::move(*value));
      }

      candidates.erase(
          std::remove_if(
              candidates.begin(), candidates.end(),
              [&](const CallCandidate& candidate) {
                for (std::size_t index = 0; index < supplied.size(); ++index) {
                  const auto& parameter =
                      candidate.fn.inputs()[candidate.parameters[index]];
                  if (!matches_parameter(parameter, supplied[index])) {
                    return true;
                  }
                }
                return false;
              }),
          candidates.end());
      if (candidates.empty()) {
        report("no compile-time overload of '" + expression.text +
               "' accepts the evaluated arguments");
        return std::nullopt;
      }
      if (candidates.size() != 1U) {
        report("compile-time call to '" + expression.text + "' is ambiguous");
        return std::nullopt;
      }

      const auto& selected = candidates.front();
      const auto inputs = selected.fn.inputs();
      std::vector<std::optional<ParamVal>> bound(inputs.size());
      for (std::size_t index = 0; index < supplied.size(); ++index) {
        bound[selected.parameters[index]] = std::move(supplied[index]);
      }
      Bindings arguments;
      std::vector<ParamVal> values;
      values.reserve(inputs.size());
      for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (!bound[index] && inputs[index].default_value) {
          bound[index] = parameter_default(inputs[index]);
        }
        if (!bound[index]) {
          report("compile-time call is missing argument '" +
                 inputs[index].name + "'");
          return std::nullopt;
        }
        values.push_back(*bound[index]);
        arguments.emplace(
            inputs[index].name,
            KnownBinding{std::move(*bound[index]), inputs[index].domain});
      }

      return evaluate_fn(selected.fn, values, arguments);
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      return expression_literal(expression, domain->element);
    }
    if (expression.kind != Kind::Reference) {
      report("invalid type expression");
      return std::nullopt;
    }
    if (domain->element == ValKind::Type) {
      auto target = declaration<Mod::TypeDecl>(expression.text);
      if (!target ||
          expression.arguments.size() > target->parameters().size()) {
        if (target) {
          report("too many arguments for type '" + expression.text + "'");
        }
        return std::nullopt;
      }
      std::vector<ParamVal> parameters;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        auto value = evaluate(expression.arguments[index],
                              target->parameters()[index], bindings);
        if (!value) {
          return std::nullopt;
        }
        parameters.push_back(std::move(*value));
      }
      auto value = environment_.type(*target, parameters);
      return value ? std::optional<ParamVal>{ParamVal(*value)} : std::nullopt;
    }
    return expression_literal(expression, domain->element);
  }

  bool unify(const TypeExpr& expression, const ParamVal& actual,
             Bindings& bindings) {
    using Kind = TypeExpr::Kind;
    if (expression.kind == Kind::Variable) {
      const auto [found, inserted] = bindings.emplace(
          expression.text, KnownBinding{actual, value_domain(actual)});
      if (!inserted && found->second.value != actual) {
        report("type variable '" + expression.text +
               "' is bound inconsistently");
        return false;
      }
      return true;
    }
    if (expression.kind == Kind::FnType) {
      const Type* callable = actual.as_type();
      const auto signature = callable_type(expression);
      if (callable == nullptr || !signature) {
        report("fn type does not match");
        return false;
      }
      const Mod::Symbol symbol = callable->schema().symbol();
      const auto parameters = TypeAccess::parameters(*callable);
      if (symbol.mod_name() != prelude_mod_name ||
          symbol.local_name() != "callable" || parameters.size() != 2U ||
          parameters[0].kind() != ParamVal::Kind::List ||
          parameters[1].kind() != ParamVal::Kind::List) {
        report("fn type does not match");
        return false;
      }
      bool valid = true;
      const std::array expected_sides{signature->inputs, signature->results};
      for (std::size_t side = 0; side < 2U; ++side) {
        const auto expected_types = expected_sides[side];
        const auto actual_types = parameters[side].elements();
        if (expected_types.size() != actual_types.size()) {
          report("fn type has the wrong arity");
          valid = false;
          continue;
        }
        for (std::size_t index = 0; index < expected_types.size(); ++index) {
          valid = unify(expected_types[index], actual_types[index], bindings) &&
                  valid;
        }
      }
      return valid;
    }
    if (expression.kind == Kind::List) {
      if (actual.kind() != ParamVal::Kind::List ||
          actual.elements().size() != expression.arguments.size()) {
        report("list type pattern does not match");
        return false;
      }
      bool valid = true;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        valid = unify(expression.arguments[index], actual.elements()[index],
                      bindings) &&
                valid;
      }
      return valid;
    }
    const Mod::Expr& computed_expression =
        expression.kind == Kind::Evaluate && expression.arguments.size() == 1U
            ? expression.arguments.front()
            : expression;
    const std::size_t field_dot = computed_expression.text.find('.');
    const auto field_generic =
        computed_expression.kind == Kind::Reference &&
                field_dot != std::string::npos
            ? std::find_if(
                  contract_ ? contract_->generics.begin()
                            : empty_generics_.begin(),
                  contract_ ? contract_->generics.end() : empty_generics_.end(),
                  [&](const auto& candidate) {
                    return candidate.name ==
                           std::string_view(computed_expression.text.data(),
                                            field_dot);
                  })
            : (contract_ ? contract_->generics.end() : empty_generics_.end());
    const auto generic_end =
        contract_ ? contract_->generics.end() : empty_generics_.end();
    const bool computed = field_generic != generic_end ||
                          computed_expression.kind == Kind::Call ||
                          computed_expression.kind == Kind::If ||
                          expression.kind == Kind::Evaluate ||
                          computed_expression.kind == Kind::Prefix ||
                          computed_expression.kind == Kind::Infix ||
                          computed_expression.kind == Kind::Postfix;
    if (computed) {
      Mod::ParamDecl expected{"computed", domain_expression(ValKind::Integer),
                              false, std::nullopt};
      if (field_generic != generic_end) {
        const auto actual_domain = value_domain(actual);
        if (!actual_domain) {
          report("derived parameter does not match the type parameter");
          return false;
        }
        expected.domain = *actual_domain;
      } else if (computed_expression.kind == Kind::Call) {
        const std::size_t receiver_dot = computed_expression.text.find('.');
        const auto generic =
            receiver_dot == std::string::npos
                ? (contract_ ? contract_->generics.end()
                             : empty_generics_.end())
                : std::find_if(contract_ ? contract_->generics.begin()
                                         : empty_generics_.begin(),
                               contract_ ? contract_->generics.end()
                                         : empty_generics_.end(),
                               [&](const auto& candidate) {
                                 return candidate.name ==
                                        std::string_view(
                                            computed_expression.text.data(),
                                            receiver_dot);
                               });
        if (generic == generic_end) {
          std::vector<CallCandidate> candidates;
          for (const auto& fn :
               environment_.fns(scope_, computed_expression.text)) {
            auto candidate = call_candidate(fn, computed_expression);
            const auto results = compiler_results(fn);
            if (!candidate || !value_inputs(fn).empty() ||
                !value_results(fn).empty() || results.size() != 1U ||
                !matches_parameter(results.front(), actual)) {
              continue;
            }
            bool accepts = true;
            for (std::size_t index = 0;
                 index < computed_expression.arguments.size(); ++index) {
              const auto domain =
                  known_domain(computed_expression.arguments[index], bindings);
              if (domain &&
                  fn.inputs()[candidate->parameters[index]].domain != *domain) {
                accepts = false;
                break;
              }
            }
            if (accepts) {
              candidates.push_back(std::move(*candidate));
            }
          }
          if (candidates.size() != 1U) {
            return false;
          }
          expected = compiler_results(candidates.front().fn).front();
        } else {
          report("type derived parameters do not take arguments");
          return false;
        }
      } else {
        if (actual.kind() == ParamVal::Kind::F64) {
          expected.domain = domain_expression(ValKind::Real);
        } else if (actual.kind() != ParamVal::Kind::I64) {
          report("computed type pattern does not match a numeric parameter");
          return false;
        }
      }
      auto value = evaluate(expression, expected, bindings);
      if (!value || *value != actual) {
        if (value) {
          report("computed type pattern does not match");
        }
        return false;
      }
      return true;
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      ValKind expected = ValKind::Integer;
      switch (actual.kind()) {
      case ParamVal::Kind::I64:
        expected = ValKind::Integer;
        break;
      case ParamVal::Kind::F64:
        expected = ValKind::Real;
        break;
      case ParamVal::Kind::Boolean:
        expected = ValKind::Boolean;
        break;
      case ParamVal::Kind::String:
        expected = ValKind::String;
        break;
      case ParamVal::Kind::Type:
      case ParamVal::Kind::List:
        report("literal type pattern does not match");
        return false;
      }
      auto value = expression_literal(expression, expected);
      if (!value || *value != actual) {
        report("literal type pattern does not match");
        return false;
      }
      return true;
    }
    if (const Type* type = actual.as_type()) {
      const auto parameters = TypeAccess::parameters(*type);
      auto target = declaration<Mod::TypeDecl>(expression.text);
      if (!target || *target != type->schema()) {
        if (target) {
          report("type pattern '" + expression.text + "' does not match '" +
                 type->schema().symbol().qualified_name() + "'");
        }
        return false;
      }
      if (expression.arguments.size() > parameters.size()) {
        report("type pattern has too many arguments");
        return false;
      }
      bool valid = true;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        valid =
            unify(expression.arguments[index], parameters[index], bindings) &&
            valid;
      }
      for (std::size_t index = expression.arguments.size();
           index < target->parameters().size(); ++index) {
        if (!target->parameters()[index].default_value ||
            parameter_default(target->parameters()[index]) !=
                std::optional<ParamVal>{parameters[index]}) {
          report("type pattern omits a non-default argument");
          valid = false;
        }
      }
      return valid;
    }
    report("type pattern does not match");
    return false;
  }

  Compiler::Limits limits_;
  Environment environment_;
  std::size_t steps_ = 0;
  std::size_t depth_ = 0;
  bool budget_reported_ = false;
  const Mod::FnDecl* schema_ = nullptr;
  Diag& diagnostics_;
  std::optional<Loc> source_;
  const FnTypeContract* contract_ = nullptr;
  const std::vector<GenericDefinition> empty_generics_;
  std::string scope_;
  std::vector<std::string> calls_;
};

}  // namespace

std::optional<ParamVal> evaluate_known_expression(
    Compiler& compiler, std::string_view scope, const Mod::Expr& expression,
    const Mod::ParamDecl& expected, const KnownBindings& bindings,
    Diag& diagnostics, std::optional<Loc> source, bool allow_host_evaluation) {
  return Solver(environment(compiler, allow_host_evaluation),
                std::string(scope), diagnostics, std::move(source))
      .evaluate_known(expression, expected, bindings);
}

std::optional<std::vector<Type>>
infer_call_types(Compiler& compiler, const Mod::FnDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParamVal>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diag& diagnostics, std::optional<Loc> source) {
  auto resolved =
      resolve_call_types(compiler, schema, arguments, known_arguments,
                         expected_results, diagnostics, std::move(source));
  return resolved
             ? std::optional<std::vector<Type>>{std::move(resolved->results)}
             : std::nullopt;
}

std::optional<std::vector<Type>>
infer_call_types(std::span<const Mod> mods, const Mod::FnDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParamVal>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diag& diagnostics, std::optional<Loc> source) {
  auto resolved =
      resolve_call_types(mods, schema, arguments, known_arguments,
                         expected_results, diagnostics, std::move(source));
  return resolved
             ? std::optional<std::vector<Type>>{std::move(resolved->results)}
             : std::nullopt;
}

std::optional<CallTypes>
resolve_call_types(Compiler& compiler, const Mod::FnDecl& schema,
                   std::span<const Type> arguments,
                   std::span<const std::optional<ParamVal>> known_arguments,
                   std::span<const std::optional<Type>> expected_results,
                   Diag& diagnostics, std::optional<Loc> source) {
  return Solver(environment(compiler), schema, diagnostics, std::move(source))
      .infer(arguments, known_arguments, expected_results);
}

std::optional<CallTypes> resolve_partial_call_types(
    Compiler& compiler, const Mod::FnDecl& schema,
    std::span<const std::optional<Type>> arguments,
    std::span<const std::optional<ParamVal>> known_arguments,
    std::span<const std::optional<Type>> expected_results, Diag& diagnostics,
    std::optional<Loc> source, bool allow_host_evaluation) {
  return Solver(environment(compiler, allow_host_evaluation), schema,
                diagnostics, std::move(source))
      .infer_partial(arguments, known_arguments, expected_results);
}

std::optional<CallTypes>
resolve_call_types(std::span<const Mod> mods, const Mod::FnDecl& schema,
                   std::span<const Type> arguments,
                   std::span<const std::optional<ParamVal>> known_arguments,
                   std::span<const std::optional<Type>> expected_results,
                   Diag& diagnostics, std::optional<Loc> source) {
  return Solver(environment(mods, diagnostics), schema, diagnostics,
                std::move(source))
      .infer(arguments, known_arguments, expected_results);
}

std::optional<std::vector<ParamVal>>
resolve_derived_parameters(Compiler& compiler, const Mod::TypeDecl& schema,
                           std::span<const ParamVal> parameters,
                           Diag& diagnostics) {
  return Solver(environment(compiler), schema, diagnostics)
      .derive(schema, parameters);
}

std::optional<std::vector<ParamVal>> resolve_derived_parameters(
    std::span<const Mod> mods, const Mod::TypeDecl& schema,
    std::span<const ParamVal> parameters, Diag& diagnostics) {
  return Solver(environment(mods, diagnostics), schema, diagnostics)
      .derive(schema, parameters);
}

}  // namespace joggle::detail
