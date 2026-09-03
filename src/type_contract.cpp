#include "type_contract.h"

#include "call_resolution.h"
#include "compiler_internal.h"
#include "domain.h"
#include "expression_syntax.h"
#include "module_internal.h"
#include "prelude.h"
#include "prelude_runtime.h"
#include "type_internal.h"

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

std::optional<Module::Expression>
known_domain(const Module::Expression& expression, const Bindings& bindings) {
  using Kind = Module::Expression::Kind;
  if (expression.kind == Kind::Variable ||
      (expression.kind == Kind::Reference && expression.arguments.empty())) {
    const auto value = bindings.find(expression.text);
    if (value == bindings.end()) {
      return std::nullopt;
    }
    switch (value->second.kind()) {
    case ParameterValue::Kind::I64:
      return domain_expression(ValueKind::Integer);
    case ParameterValue::Kind::F64:
      return domain_expression(ValueKind::Real);
    case ParameterValue::Kind::Boolean:
      return domain_expression(ValueKind::Boolean);
    case ParameterValue::Kind::String:
      return domain_expression(ValueKind::String);
    case ParameterValue::Kind::Type:
      return domain_expression(ValueKind::Type);
    case ParameterValue::Kind::Attribute:
      return domain_expression(ValueKind::Attribute);
    case ParameterValue::Kind::List:
      return std::nullopt;
    }
  }
  if (expression.kind == Kind::Number) {
    return domain_expression(expression.text.find_first_of(".eE") ==
                                     std::string::npos
                                 ? ValueKind::Integer
                                 : ValueKind::Real);
  }
  if (expression.kind == Kind::Boolean) {
    return domain_expression(ValueKind::Boolean);
  }
  if (expression.kind == Kind::String) {
    return domain_expression(ValueKind::String);
  }
  return std::nullopt;
}

struct Environment {
  using Evaluator = std::function<std::optional<ParameterValue>(
      Module::FunctionDecl, std::span<const ParameterValue>)>;
  using EvaluationCheck = std::function<bool(const Module::FunctionDecl&)>;
  using FunctionLookup = std::function<std::vector<Module::FunctionDecl>(
      std::string_view, std::string_view)>;
  using OperatorLookup = std::function<std::vector<Module::FunctionDecl>(
      std::string_view, std::string_view, Module::FunctionDecl::Fixity)>;
  std::function<std::optional<Module>(std::string_view)> module;
  std::function<std::optional<Type>(const Module::TypeDecl&,
                                    std::span<const ParameterValue>)>
      type;
  std::function<std::optional<Attribute>(const Module::AttributeDecl&,
                                         std::span<const ParameterValue>)>
      attribute;
  std::function<bool(const Module::TypeDecl&, const Module::InterfaceDecl&)>
      conforms;
  FunctionLookup functions;
  OperatorLookup operators;
  EvaluationCheck can_evaluate;
  Evaluator evaluate;
  bool require_hermetic_host_evaluation = false;
  Compiler::EvaluationLimits limits;
};

Environment environment(Compiler& compiler, bool allow_host_evaluation = true) {
  return {[&](std::string_view name) { return compiler.module(name); },
          [&](const Module::TypeDecl& schema,
              std::span<const ParameterValue> parameters) {
            return CompilerAccess::make(
                compiler, schema, std::span<const ParameterValue>(parameters));
          },
          [&](const Module::AttributeDecl& schema,
              std::span<const ParameterValue> parameters) {
            return CompilerAccess::make(
                compiler, schema, std::span<const ParameterValue>(parameters));
          },
          [&](const Module::TypeDecl& declaration,
              const Module::InterfaceDecl& interface) {
            return compiler.conforms(declaration, interface);
          },
          [&](std::string_view owner, std::string_view reference) {
            return visible_functions(compiler, owner, reference);
          },
          [&](std::string_view owner, std::string_view symbol,
              Module::FunctionDecl::Fixity fixity) {
            return visible_operators(compiler, owner, symbol, fixity);
          },
          [&, under_residual_control = !allow_host_evaluation](
              const Module::FunctionDecl& function) {
            return CompilerAccess::can_evaluate(compiler, function,
                                                under_residual_control);
          },
          Environment::Evaluator{
              [&, under_residual_control = !allow_host_evaluation](
                  Module::FunctionDecl function,
                  std::span<const ParameterValue> arguments) {
                return CompilerAccess::evaluate(compiler, std::move(function),
                                                arguments,
                                                under_residual_control);
              }},
          !allow_host_evaluation,
          CompilerAccess::limits(compiler)};
}

Environment environment(std::span<const Module> modules,
                        Diagnostics& diagnostics) {
  const auto find = [modules](std::string_view name) -> std::optional<Module> {
    const auto module =
        std::find_if(modules.begin(), modules.end(),
                     [&](const Module& value) { return value.name() == name; });
    return module == modules.end() ? std::nullopt
                                   : std::optional<Module>{*module};
  };
  const auto conforms = [find](const Module::TypeDecl& declaration,
                               const Module::InterfaceDecl& interface) {
    const auto owner = find(declaration.symbol().module_name());
    if (!owner) {
      return false;
    }
    for (const std::string& reference : declaration.interfaces()) {
      const std::size_t dot = reference.find('.');
      std::string_view module_name = owner->name();
      std::string_view local_name = reference;
      if (dot != std::string::npos) {
        const std::string_view prefix(reference.data(), dot);
        local_name = std::string_view(reference).substr(dot + 1U);
        const auto imported =
            std::find_if(owner->imports().begin(), owner->imports().end(),
                         [&](const Module::Import& import) {
                           return import.prefix() == prefix;
                         });
        module_name =
            imported == owner->imports().end() ? prefix : imported->name;
      }
      if (module_name == interface.symbol().module_name() &&
          local_name == interface.name()) {
        return true;
      }
    }
    return false;
  };
  return {
      find,
      [modules, &diagnostics](
          const Module::TypeDecl& schema,
          std::span<const ParameterValue> parameters) -> std::optional<Type> {
        auto values =
            validate_parameters(schema.symbol().qualified_name(),
                                schema.parameters(), parameters, diagnostics);
        if (!values) {
          return std::nullopt;
        }
        auto derived =
            resolve_derived_parameters(modules, schema, *values, diagnostics);
        return derived ? std::optional<Type>{TypeAccess::make(
                             schema, std::move(*values), std::move(*derived))}
                       : std::nullopt;
      },
      [&diagnostics](const Module::AttributeDecl& schema,
                     std::span<const ParameterValue> parameters)
          -> std::optional<Attribute> {
        auto values =
            validate_parameters(schema.symbol().qualified_name(),
                                schema.parameters(), parameters, diagnostics);
        return values ? std::optional<Attribute>{TypeAccess::make(
                            schema, std::move(*values))}
                      : std::nullopt;
      },
      conforms,
      [modules](std::string_view owner, std::string_view reference) {
        return visible_functions(modules, owner, reference);
      },
      [modules](std::string_view owner, std::string_view symbol,
                Module::FunctionDecl::Fixity fixity) {
        return visible_operators(modules, owner, symbol, fixity);
      },
      [](const Module::FunctionDecl& function) {
        return is_prelude_primitive(function);
      },
      [&diagnostics](Module::FunctionDecl function,
                     std::span<const ParameterValue> arguments) {
        const Compiler::EvaluationLimits limits;
        return evaluate_prelude_primitive(function, arguments, diagnostics,
                                          limits.steps);
      },
      false,
      {}};
}

class Solver {
public:
  Solver(Environment environment, const Module::FunctionDecl& schema,
         Diagnostics& diagnostics, std::optional<SourceRange> source)
      : limits_(environment.limits), environment_(std::move(environment)),
        schema_(&schema), diagnostics_(diagnostics), source_(std::move(source)),
        contract_(&FunctionTypeAccess::get(schema)),
        scope_(schema.symbol().module_name()) {}

  Solver(Environment environment, const Module::TypeDecl& schema,
         Diagnostics& diagnostics)
      : limits_(environment.limits), environment_(std::move(environment)),
        diagnostics_(diagnostics), scope_(schema.symbol().module_name()) {}

  Solver(Environment environment, std::string scope, Diagnostics& diagnostics,
         std::optional<SourceRange> source)
      : limits_(environment.limits), environment_(std::move(environment)),
        diagnostics_(diagnostics), source_(std::move(source)),
        scope_(std::move(scope)) {}

  std::optional<ParameterValue>
  evaluate_known(const Module::Expression& expression,
                 const Module::ParameterDecl& expected,
                 const Bindings& bindings) {
    return evaluate(expression, expected, bindings);
  }

  std::optional<CallTypes>
  infer(std::span<const Type> arguments,
        std::span<const std::optional<ParameterValue>> known_arguments,
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
                std::span<const std::optional<ParameterValue>> known_arguments,
                std::span<const std::optional<Type>> expected) {
    if (schema_ == nullptr || contract_ == nullptr) {
      report("call solver has no function declaration");
      return std::nullopt;
    }
    const auto known_inputs = parameter_inputs(*schema_);
    const auto value_inputs = ir_inputs(*schema_);
    const auto value_results = ir_results(*schema_);
    if (known_arguments.size() != known_inputs.size()) {
      report("call Known-argument map does not match its function");
      return std::nullopt;
    }
    Bindings bindings;
    std::size_t argument = 0;
    for (const auto& input : value_inputs) {
      const std::size_t count =
          input.variadic ? arguments.size() - argument : 1U;
      if (argument + count > arguments.size()) {
        report("function call has too few arguments");
        return std::nullopt;
      }
      for (std::size_t item = 0; item < count; ++item) {
        const auto& actual = arguments[argument++];
        if (actual && !unify(input.domain, ParameterValue(*actual), bindings)) {
          return std::nullopt;
        }
      }
    }
    if (argument != arguments.size()) {
      report("function call has too many arguments");
      return std::nullopt;
    }
    std::size_t known_index = 0;
    for (std::size_t input_index = 0; input_index < schema_->inputs().size();
         ++input_index) {
      const auto& input = schema_->inputs()[input_index];
      if (contract_->ir_inputs[input_index]) {
        continue;
      }
      std::optional<ParameterValue> actual = known_arguments[known_index++];
      if (!actual && input.default_value) {
        actual = parameter_default(input);
      }
      if (!actual) {
        if (!contract_->bindings.empty() && contract_->bindings[input_index]) {
          report("function call is missing Known argument '" + input.name +
                 "'");
          return std::nullopt;
        }
        continue;
      }
      if (!matches_parameter(input, *actual)) {
        report("function call Known argument '" + input.name +
               "' has the wrong domain");
        return std::nullopt;
      }
      if (!contract_->bindings.empty() && contract_->bindings[input_index] &&
          !unify(*contract_->bindings[input_index], *actual, bindings)) {
        return std::nullopt;
      }
    }
    if (expected.size() != value_results.size()) {
      report("function call result count does not match its declaration");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (expected[index] &&
          !unify(value_results[index].domain, ParameterValue(*expected[index]),
                 bindings)) {
        return std::nullopt;
      }
    }

    const Module::ParameterDecl type_parameter{
        "result", domain_expression(ValueKind::Type), false, std::nullopt};
    CallTypes resolved;
    resolved.arguments.reserve(arguments.size());
    argument = 0;
    for (const auto& input : value_inputs) {
      const std::size_t count =
          input.variadic ? arguments.size() - argument : 1U;
      for (std::size_t item = 0; item < count; ++item) {
        if (arguments[argument]) {
          resolved.arguments.push_back(*arguments[argument++]);
          continue;
        }
        auto value = evaluate(input.domain, type_parameter, bindings);
        if (!value || value->as_type() == nullptr) {
          return std::nullopt;
        }
        resolved.arguments.push_back(*value->as_type());
        ++argument;
      }
    }
    resolved.results.reserve(value_results.size());
    for (const auto& result : value_results) {
      auto value = evaluate(result.domain, type_parameter, bindings);
      if (!value || value->as_type() == nullptr) {
        return std::nullopt;
      }
      resolved.results.push_back(*value->as_type());
    }
    return resolved;
  }

  std::optional<std::vector<ParameterValue>>
  derive(const Module::TypeDecl& schema,
         std::span<const ParameterValue> parameters) {
    if (parameters.size() != schema.parameters().size()) {
      report("type instance has an invalid parameter binding");
      return std::nullopt;
    }
    Bindings bindings;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      bindings.emplace(schema.parameters()[index].name, parameters[index]);
    }
    std::vector<ParameterValue> values;
    values.reserve(schema.derived_parameters().size());
    for (const auto& derived : schema.derived_parameters()) {
      std::vector<Module::ParameterDecl> matches;
      for (const std::string& reference : schema.interfaces()) {
        const auto interface = interface_declaration(reference);
        if (!interface) {
          return std::nullopt;
        }
        const auto field =
            std::find_if(interface->fields().begin(), interface->fields().end(),
                         [&](const auto& candidate) {
                           return candidate.name == derived.name;
                         });
        if (field != interface->fields().end()) {
          matches.push_back(*field);
        }
      }
      if (matches.size() != 1U) {
        report("derived parameter '" + derived.name + "' on type '" +
               schema.symbol().qualified_name() +
               "' does not resolve to exactly one interface field");
        return std::nullopt;
      }
      auto value = evaluate(derived.value, matches.front(), bindings);
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
      const auto scope = environment_.module(scope_);
      if (scope) {
        const auto imported =
            std::find_if(scope->imports().begin(), scope->imports().end(),
                         [&](const Module::Import& import) {
                           return import.prefix() == prefix;
                         });
        if (imported != scope->imports().end()) {
          owner = imported->name;
        }
      }
    }
    const std::string_view local =
        dot == std::string_view::npos ? reference : reference.substr(dot + 1U);
    const auto module = environment_.module(owner);
    if (!module) {
      report("type contract references unknown module '" + std::string(owner) +
             "'");
      return std::nullopt;
    }
    std::optional<Declaration> result;
    if constexpr (std::is_same_v<Declaration, Module::TypeDecl>) {
      result = module->type(local);
    } else if constexpr (std::is_same_v<Declaration, Module::FunctionDecl>) {
      result = module->function(local);
    } else {
      result = module->attribute(local);
    }
    if (!result) {
      report("type contract references unknown declaration '" +
             std::string(reference) + "'");
    }
    return result;
  }

  std::optional<Module::InterfaceDecl>
  interface_declaration(std::string_view reference) {
    const std::size_t dot = reference.find('.');
    std::string_view owner = scope_;
    if (dot != std::string_view::npos) {
      const std::string_view prefix = reference.substr(0, dot);
      owner = prefix;
      const auto scope = environment_.module(scope_);
      if (scope) {
        const auto imported =
            std::find_if(scope->imports().begin(), scope->imports().end(),
                         [&](const Module::Import& import) {
                           return import.prefix() == prefix;
                         });
        if (imported != scope->imports().end()) {
          owner = imported->name;
        }
      }
    }
    const std::string_view local =
        dot == std::string_view::npos ? reference : reference.substr(dot + 1U);
    const auto module = environment_.module(owner);
    if (!module) {
      report("generic constraint references unknown module '" +
             std::string(owner) + "'");
      return std::nullopt;
    }
    const auto result = module->interface(local);
    if (!result) {
      report("generic constraint references unknown interface '" +
             std::string(reference) + "'");
    }
    return result;
  }

  bool satisfies_constraint(std::string_view variable,
                            const ParameterValue& actual) {
    const auto generic = std::find_if(
        contract_ ? contract_->generics.begin() : empty_generics_.begin(),
        contract_ ? contract_->generics.end() : empty_generics_.end(),
        [&](const GenericDefinition& candidate) {
          return candidate.name == variable;
        });
    const auto generic_end =
        contract_ ? contract_->generics.end() : empty_generics_.end();
    if (generic == generic_end || !generic->constraint) {
      return true;
    }
    const Type* type = actual.as_type();
    const auto interface = interface_declaration(*generic->constraint);
    if (type == nullptr || !interface ||
        !environment_.conforms(type->schema(), *interface)) {
      if (interface) {
        report("type bound to '" + std::string(variable) +
               "' does not implement interface '" + *generic->constraint + "'");
      }
      return false;
    }
    return true;
  }

  std::optional<ParameterValue>
  expression_literal(const TypeExpression& expression, ValueKind expected) {
    using Kind = TypeExpression::Kind;
    if (expected == ValueKind::Integer && expression.kind == Kind::Number) {
      std::int64_t value = 0;
      const auto parsed = std::from_chars(
          expression.text.data(),
          expression.text.data() + expression.text.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == expression.text.data() + expression.text.size()) {
        return ParameterValue(value);
      }
    } else if (expected == ValueKind::Real && expression.kind == Kind::Number) {
      double value = 0.0;
      std::istringstream input(expression.text);
      input.imbue(std::locale::classic());
      input >> value;
      if (input && input.peek() == std::char_traits<char>::eof()) {
        return ParameterValue(value);
      }
    } else if (expected == ValueKind::Boolean &&
               expression.kind == Kind::Boolean) {
      return ParameterValue(expression.text == "true");
    } else if (expected == ValueKind::String &&
               expression.kind == Kind::String) {
      return ParameterValue(expression.text);
    }
    report("type expression literal has the wrong kind");
    return std::nullopt;
  }

  std::optional<ParameterValue> evaluate_derived_parameter(
      const GenericDefinition& generic, std::string_view field_name,
      const Module::ParameterDecl& expected, const Bindings& bindings) {
    if (!generic.constraint) {
      report("generic '" + generic.name +
             "' has no interface exposing derived parameter '" +
             std::string(field_name) + "'");
      return std::nullopt;
    }
    const auto bound = bindings.find(generic.name);
    const Type* type =
        bound == bindings.end() ? nullptr : bound->second.as_type();
    if (type == nullptr) {
      report("cannot evaluate derived parameter '" + generic.name + "." +
             std::string(field_name) +
             "' before its receiver type is inferred");
      return std::nullopt;
    }
    const auto interface = interface_declaration(*generic.constraint);
    const auto field =
        interface ? std::find_if(interface->fields().begin(),
                                 interface->fields().end(),
                                 [&](const auto& candidate) {
                                   return candidate.name == field_name;
                                 })
                  : std::span<const Module::ParameterDecl>::iterator{};
    if (!interface || field == interface->fields().end() ||
        field->domain != expected.domain) {
      report("ill-typed derived parameter '" + generic.name + "." +
             std::string(field_name) + "'");
      return std::nullopt;
    }
    if (!environment_.conforms(type->schema(), *interface)) {
      report("type bound to '" + generic.name +
             "' does not implement interface '" + *generic.constraint + "'");
      return std::nullopt;
    }

    const auto derived = std::find_if(
        type->schema().derived_parameters().begin(),
        type->schema().derived_parameters().end(),
        [&](const auto& candidate) { return candidate.name == field_name; });
    if (derived == type->schema().derived_parameters().end()) {
      const auto parameter = std::find_if(
          type->schema().parameters().begin(),
          type->schema().parameters().end(), [&](const auto& candidate) {
            return candidate.name == field_name &&
                   candidate.domain == field->domain;
          });
      if (parameter != type->schema().parameters().end()) {
        const auto index = static_cast<std::size_t>(
            std::distance(type->schema().parameters().begin(), parameter));
        const auto values = TypeAccess::parameters(*type);
        return index < values.size()
                   ? std::optional<ParameterValue>{values[index]}
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
      arguments.emplace(type->schema().parameters()[index].name,
                        parameters[index]);
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
    scope_ = std::string(type->schema().symbol().module_name());
    auto value = evaluate(derived->value, *field, arguments);
    scope_ = caller_scope;
    calls_.pop_back();
    return value;
  }

  std::vector<Module::FunctionDecl> operator_declarations(
      std::string_view symbol, Module::FunctionDecl::Fixity fixity,
      const Module::ParameterDecl& expected, std::size_t arity) {
    std::vector<Module::FunctionDecl> result;
    for (const auto& candidate :
         environment_.operators(scope_, symbol, fixity)) {
      const auto known_inputs = parameter_inputs(candidate);
      const auto known_results = parameter_results(candidate);
      if (!ir_inputs(candidate).empty() || !ir_results(candidate).empty() ||
          known_inputs.size() != arity || known_results.size() != 1U ||
          known_results.front().domain != expected.domain) {
        continue;
      }
      result.push_back(candidate);
    }
    return result;
  }

  std::optional<ParameterValue>
  evaluate_function(const Module::FunctionDecl& function,
                    std::span<const ParameterValue> values,
                    const Bindings& arguments) {
    const std::string identity = function.symbol().stable_name();
    if (std::find(calls_.begin(), calls_.end(), identity) != calls_.end()) {
      report("recursive compile-time call to '" +
             function.symbol().qualified_name() + "'");
      return std::nullopt;
    }
    calls_.push_back(identity);
    struct CallGuard {
      std::vector<std::string>& calls;
      ~CallGuard() { calls.pop_back(); }
    } guard{calls_};

    if (environment_.require_hermetic_host_evaluation &&
        (!environment_.can_evaluate || !environment_.can_evaluate(function))) {
      report("host implementation of function '" +
             function.symbol().qualified_name() +
             "' is guarded and cannot execute under Residual control");
      return std::nullopt;
    }
    if (environment_.evaluate &&
        (!environment_.can_evaluate || environment_.can_evaluate(function))) {
      return environment_.evaluate(function, values);
    }

    const Module::Expression* body = ModuleAccess::expression(function);
    const auto results = parameter_results(function);
    if (body == nullptr || results.size() != 1U) {
      report("compile-time function '" + function.symbol().qualified_name() +
             "' has no available evaluator");
      return std::nullopt;
    }
    struct ScopeGuard {
      std::string& scope;
      std::string caller;
      ~ScopeGuard() { scope = std::move(caller); }
    } scope_guard{scope_, scope_};
    scope_ = std::string(function.symbol().module_name());
    return evaluate(*body, results.front(), arguments);
  }

  std::optional<ParameterValue> evaluate(const TypeExpression& expression,
                                         const Module::ParameterDecl& expected,
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
    using Kind = TypeExpression::Kind;
    const auto domain = kernel_domain(expected.domain);
    if (!domain) {
      report("unknown parameter domain for '" + expected.name + "'");
      return std::nullopt;
    }
    if (expression.kind == Kind::FunctionType) {
      const auto signature = callable_type(expression);
      if (domain->list || domain->element != ValueKind::Type || !signature) {
        report("malformed function type expression");
        return std::nullopt;
      }
      const Module::ParameterDecl type_element{
          "signature element", domain_expression(ValueKind::Type), false,
          std::nullopt};
      std::vector<ParameterValue> parameters;
      parameters.reserve(2U);
      for (const auto side : {signature->inputs, signature->results}) {
        std::vector<ParameterValue> types;
        types.reserve(side.size());
        for (const auto& element : side) {
          auto type = evaluate(element, type_element, bindings);
          if (!type || type->as_type() == nullptr) {
            return std::nullopt;
          }
          types.push_back(std::move(*type));
        }
        parameters.push_back(ParameterValue::list(std::move(types)));
      }
      auto callable = declaration<Module::TypeDecl>("prelude.callable");
      auto value = callable ? environment_.type(*callable, parameters)
                            : std::optional<Type>{};
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    if (expression.kind == Kind::Variable) {
      const auto found = bindings.find(expression.text);
      if (found == bindings.end()) {
        report("cannot infer type variable '" + expression.text + "'");
        return std::nullopt;
      }
      return found->second;
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
      const Module::ParameterDecl condition{
          "condition", domain_expression(ValueKind::Boolean), false,
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
      Module::ParameterDecl element = expected;
      element.domain = domain_expression(domain->element);
      std::vector<ParameterValue> values;
      for (const auto& argument : expression.arguments) {
        auto value = evaluate(argument, element, bindings);
        if (!value) {
          return std::nullopt;
        }
        values.push_back(std::move(*value));
      }
      return ParameterValue::list(std::move(values));
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
      const auto fixity = expression.kind == Kind::Prefix
                              ? Module::FunctionDecl::Fixity::Prefix
                          : expression.kind == Kind::Postfix
                              ? Module::FunctionDecl::Fixity::Postfix
                              : Module::FunctionDecl::Fixity::Infix;
      auto overloads =
          operator_declarations(expression.text, fixity, expected, arity);
      overloads.erase(
          std::remove_if(overloads.begin(), overloads.end(),
                         [&](const Module::FunctionDecl& candidate) {
                           const auto inputs = parameter_inputs(candidate);
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
        const auto function = overloads.front();
        Bindings arguments;
        std::vector<ParameterValue> values;
        values.reserve(arity);
        const auto inputs = parameter_inputs(function);
        for (std::size_t index = 0; index < arity; ++index) {
          auto value =
              evaluate(expression.arguments[index], inputs[index], bindings);
          if (!value) {
            return std::nullopt;
          }
          values.push_back(*value);
          arguments.emplace(inputs[index].name, std::move(*value));
        }
        return evaluate_function(function, values, arguments);
      }
      report("no matching compile-time operator '" + expression.text + "'");
      return std::nullopt;
    }
    if (expression.kind == Kind::Call) {
      std::vector<CallCandidate> candidates;
      for (const auto& function :
           environment_.functions(scope_, expression.text)) {
        auto candidate = call_candidate(function, expression);
        const auto results = parameter_results(function);
        if (!candidate || !ir_inputs(function).empty() ||
            !ir_results(function).empty() || results.size() != 1U ||
            results.front().domain != expected.domain) {
          continue;
        }
        bool accepts_known_domains = true;
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          const auto actual =
              known_domain(expression.arguments[index], bindings);
          if (actual &&
              function.inputs()[candidate->parameters[index]].domain !=
                  *actual) {
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

      std::vector<ParameterValue> supplied;
      supplied.reserve(expression.arguments.size());
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        const auto& first = candidates.front();
        const auto& first_parameter =
            first.function.inputs()[first.parameters[index]];
        const bool common = std::all_of(
            candidates.begin() + 1, candidates.end(),
            [&](const CallCandidate& current) {
              return current.function.inputs()[current.parameters[index]]
                         .domain == first_parameter.domain;
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
                      candidate.function.inputs()[candidate.parameters[index]];
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
      const auto inputs = selected.function.inputs();
      std::vector<std::optional<ParameterValue>> bound(inputs.size());
      for (std::size_t index = 0; index < supplied.size(); ++index) {
        bound[selected.parameters[index]] = std::move(supplied[index]);
      }
      Bindings arguments;
      std::vector<ParameterValue> values;
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
        arguments.emplace(inputs[index].name, std::move(*bound[index]));
      }

      return evaluate_function(selected.function, values, arguments);
    }
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      return expression_literal(expression, domain->element);
    }
    if (expression.kind != Kind::Reference) {
      report("invalid type expression");
      return std::nullopt;
    }
    if (domain->element == ValueKind::Type) {
      auto target = declaration<Module::TypeDecl>(expression.text);
      if (!target ||
          expression.arguments.size() > target->parameters().size()) {
        if (target) {
          report("too many arguments for type '" + expression.text + "'");
        }
        return std::nullopt;
      }
      std::vector<ParameterValue> parameters;
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
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    if (domain->element == ValueKind::Attribute) {
      auto target = declaration<Module::AttributeDecl>(expression.text);
      if (!target ||
          expression.arguments.size() > target->parameters().size()) {
        if (target) {
          report("too many arguments for attribute '" + expression.text + "'");
        }
        return std::nullopt;
      }
      std::vector<ParameterValue> parameters;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        auto value = evaluate(expression.arguments[index],
                              target->parameters()[index], bindings);
        if (!value) {
          return std::nullopt;
        }
        parameters.push_back(std::move(*value));
      }
      auto value = environment_.attribute(*target, parameters);
      return value ? std::optional<ParameterValue>{ParameterValue(*value)}
                   : std::nullopt;
    }
    return expression_literal(expression, domain->element);
  }

  bool unify(const TypeExpression& expression, const ParameterValue& actual,
             Bindings& bindings) {
    using Kind = TypeExpression::Kind;
    if (expression.kind == Kind::Variable) {
      if (!satisfies_constraint(expression.text, actual)) {
        return false;
      }
      const auto [found, inserted] = bindings.emplace(expression.text, actual);
      if (!inserted && found->second != actual) {
        report("type variable '" + expression.text +
               "' is bound inconsistently");
        return false;
      }
      return true;
    }
    if (expression.kind == Kind::FunctionType) {
      const Type* callable = actual.as_type();
      const auto signature = callable_type(expression);
      if (callable == nullptr || !signature) {
        report("function type does not match");
        return false;
      }
      const Module::Symbol symbol = callable->schema().symbol();
      const auto parameters = TypeAccess::parameters(*callable);
      if (symbol.module_name() != prelude_module_name ||
          symbol.local_name() != "callable" || parameters.size() != 2U ||
          parameters[0].kind() != ParameterValue::Kind::List ||
          parameters[1].kind() != ParameterValue::Kind::List) {
        report("function type does not match");
        return false;
      }
      bool valid = true;
      const std::array expected_sides{signature->inputs, signature->results};
      for (std::size_t side = 0; side < 2U; ++side) {
        const auto expected_types = expected_sides[side];
        const auto actual_types = parameters[side].elements();
        if (expected_types.size() != actual_types.size()) {
          report("function type has the wrong arity");
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
      if (actual.kind() != ParameterValue::Kind::List ||
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
    const std::size_t field_dot = expression.text.find('.');
    const auto field_generic =
        expression.kind == Kind::Reference && field_dot != std::string::npos
            ? std::find_if(
                  contract_ ? contract_->generics.begin()
                            : empty_generics_.begin(),
                  contract_ ? contract_->generics.end() : empty_generics_.end(),
                  [&](const auto& candidate) {
                    return candidate.name ==
                           std::string_view(expression.text.data(), field_dot);
                  })
            : (contract_ ? contract_->generics.end() : empty_generics_.end());
    const auto generic_end =
        contract_ ? contract_->generics.end() : empty_generics_.end();
    const bool computed =
        field_generic != generic_end || expression.kind == Kind::Call ||
        expression.kind == Kind::If || expression.kind == Kind::Evaluate ||
        expression.kind == Kind::Prefix || expression.kind == Kind::Infix ||
        expression.kind == Kind::Postfix;
    if (computed) {
      Module::ParameterDecl expected{"computed",
                                     domain_expression(ValueKind::Integer),
                                     false, std::nullopt};
      if (field_generic != generic_end) {
        const auto interface =
            field_generic->constraint
                ? interface_declaration(*field_generic->constraint)
                : std::optional<Module::InterfaceDecl>{};
        const std::string_view field_name =
            std::string_view(expression.text).substr(field_dot + 1U);
        const auto field =
            interface ? std::find_if(interface->fields().begin(),
                                     interface->fields().end(),
                                     [&](const auto& candidate) {
                                       return candidate.name == field_name;
                                     })
                      : std::span<const Module::ParameterDecl>::iterator{};
        if (!interface || field == interface->fields().end()) {
          report("unknown derived parameter '" + expression.text + "'");
          return false;
        }
        expected = *field;
        if (!matches_parameter(expected, actual)) {
          report("derived parameter does not match the type parameter");
          return false;
        }
      } else if (expression.kind == Kind::Call) {
        const std::size_t receiver_dot = expression.text.find('.');
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
                                        std::string_view(expression.text.data(),
                                                         receiver_dot);
                               });
        if (generic == generic_end) {
          std::vector<CallCandidate> candidates;
          for (const auto& function :
               environment_.functions(scope_, expression.text)) {
            auto candidate = call_candidate(function, expression);
            const auto results = parameter_results(function);
            if (!candidate || !ir_inputs(function).empty() ||
                !ir_results(function).empty() || results.size() != 1U ||
                !matches_parameter(results.front(), actual)) {
              continue;
            }
            bool accepts = true;
            for (std::size_t index = 0; index < expression.arguments.size();
                 ++index) {
              const auto domain =
                  known_domain(expression.arguments[index], bindings);
              if (domain &&
                  function.inputs()[candidate->parameters[index]].domain !=
                      *domain) {
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
          expected = parameter_results(candidates.front().function).front();
        } else {
          report("type derived parameters do not take arguments");
          return false;
        }
      } else {
        if (actual.kind() == ParameterValue::Kind::F64) {
          expected.domain = domain_expression(ValueKind::Real);
        } else if (actual.kind() != ParameterValue::Kind::I64) {
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
      ValueKind expected = ValueKind::Integer;
      switch (actual.kind()) {
      case ParameterValue::Kind::I64:
        expected = ValueKind::Integer;
        break;
      case ParameterValue::Kind::F64:
        expected = ValueKind::Real;
        break;
      case ParameterValue::Kind::Boolean:
        expected = ValueKind::Boolean;
        break;
      case ParameterValue::Kind::String:
        expected = ValueKind::String;
        break;
      case ParameterValue::Kind::Type:
      case ParameterValue::Kind::Attribute:
      case ParameterValue::Kind::List:
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
      auto target = declaration<Module::TypeDecl>(expression.text);
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
                std::optional<ParameterValue>{parameters[index]}) {
          report("type pattern omits a non-default argument");
          valid = false;
        }
      }
      return valid;
    }
    if (const Attribute* attribute = actual.as_attribute()) {
      const auto parameters = TypeAccess::parameters(*attribute);
      auto target = declaration<Module::AttributeDecl>(expression.text);
      if (!target || *target != attribute->schema()) {
        if (target) {
          report("attribute pattern '" + expression.text + "' does not match");
        }
        return false;
      }
      if (expression.arguments.size() > parameters.size()) {
        report("attribute pattern has too many arguments");
        return false;
      }
      bool valid = true;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        valid =
            unify(expression.arguments[index], parameters[index], bindings) &&
            valid;
      }
      return valid;
    }
    report("type pattern does not match");
    return false;
  }

  Compiler::EvaluationLimits limits_;
  Environment environment_;
  std::size_t steps_ = 0;
  std::size_t depth_ = 0;
  bool budget_reported_ = false;
  const Module::FunctionDecl* schema_ = nullptr;
  Diagnostics& diagnostics_;
  std::optional<SourceRange> source_;
  const FunctionTypeContract* contract_ = nullptr;
  const std::vector<GenericDefinition> empty_generics_;
  std::string scope_;
  std::vector<std::string> calls_;
};

}  // namespace

std::optional<ParameterValue> evaluate_known_expression(
    Compiler& compiler, std::string_view scope,
    const Module::Expression& expression, const Module::ParameterDecl& expected,
    const KnownBindings& bindings, Diagnostics& diagnostics,
    std::optional<SourceRange> source, bool allow_host_evaluation) {
  return Solver(environment(compiler, allow_host_evaluation),
                std::string(scope), diagnostics, std::move(source))
      .evaluate_known(expression, expected, bindings);
}

std::optional<std::vector<Type>>
infer_call_types(Compiler& compiler, const Module::FunctionDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParameterValue>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diagnostics& diagnostics, std::optional<SourceRange> source) {
  auto resolved =
      resolve_call_types(compiler, schema, arguments, known_arguments,
                         expected_results, diagnostics, std::move(source));
  return resolved
             ? std::optional<std::vector<Type>>{std::move(resolved->results)}
             : std::nullopt;
}

std::optional<std::vector<Type>>
infer_call_types(std::span<const Module> modules,
                 const Module::FunctionDecl& schema,
                 std::span<const Type> arguments,
                 std::span<const std::optional<ParameterValue>> known_arguments,
                 std::span<const std::optional<Type>> expected_results,
                 Diagnostics& diagnostics, std::optional<SourceRange> source) {
  auto resolved =
      resolve_call_types(modules, schema, arguments, known_arguments,
                         expected_results, diagnostics, std::move(source));
  return resolved
             ? std::optional<std::vector<Type>>{std::move(resolved->results)}
             : std::nullopt;
}

std::optional<CallTypes> resolve_call_types(
    Compiler& compiler, const Module::FunctionDecl& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source) {
  return Solver(environment(compiler), schema, diagnostics, std::move(source))
      .infer(arguments, known_arguments, expected_results);
}

std::optional<CallTypes> resolve_partial_call_types(
    Compiler& compiler, const Module::FunctionDecl& schema,
    std::span<const std::optional<Type>> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source,
    bool allow_host_evaluation) {
  return Solver(environment(compiler, allow_host_evaluation), schema,
                diagnostics, std::move(source))
      .infer_partial(arguments, known_arguments, expected_results);
}

std::optional<CallTypes> resolve_call_types(
    std::span<const Module> modules, const Module::FunctionDecl& schema,
    std::span<const Type> arguments,
    std::span<const std::optional<ParameterValue>> known_arguments,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source) {
  return Solver(environment(modules, diagnostics), schema, diagnostics,
                std::move(source))
      .infer(arguments, known_arguments, expected_results);
}

std::optional<std::vector<ParameterValue>>
resolve_derived_parameters(Compiler& compiler, const Module::TypeDecl& schema,
                           std::span<const ParameterValue> parameters,
                           Diagnostics& diagnostics) {
  return Solver(environment(compiler), schema, diagnostics)
      .derive(schema, parameters);
}

std::optional<std::vector<ParameterValue>> resolve_derived_parameters(
    std::span<const Module> modules, const Module::TypeDecl& schema,
    std::span<const ParameterValue> parameters, Diagnostics& diagnostics) {
  return Solver(environment(modules, diagnostics), schema, diagnostics)
      .derive(schema, parameters);
}

}  // namespace joggle::detail
