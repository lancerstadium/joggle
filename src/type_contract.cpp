#include "type_contract.h"

#include "compiler_internal.h"
#include "domain.h"
#include "module_internal.h"
#include "type_internal.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_map>

namespace joggle::detail {
namespace {

using Bindings = std::unordered_map<std::string, ParameterValue>;

std::optional<std::int64_t> checked_add(std::int64_t left,
                                        std::int64_t right) {
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if ((right > 0 && left > maximum - right) ||
      (right < 0 && left < minimum - right)) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<std::int64_t> checked_subtract(std::int64_t left,
                                             std::int64_t right) {
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if ((right > 0 && left < minimum + right) ||
      (right < 0 && left > maximum + right)) {
    return std::nullopt;
  }
  return left - right;
}

std::uint64_t magnitude(std::int64_t value) {
  return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                   : static_cast<std::uint64_t>(value);
}

std::optional<std::int64_t> checked_multiply(std::int64_t left,
                                             std::int64_t right) {
  const bool negative = (left < 0) != (right < 0);
  const std::uint64_t left_magnitude = magnitude(left);
  const std::uint64_t right_magnitude = magnitude(right);
  constexpr std::uint64_t negative_limit = std::uint64_t{1} << 63U;
  constexpr std::uint64_t positive_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const std::uint64_t limit = negative ? negative_limit : positive_limit;
  if (right_magnitude != 0U &&
      left_magnitude > limit / right_magnitude) {
    return std::nullopt;
  }
  const std::uint64_t product = left_magnitude * right_magnitude;
  if (!negative) {
    return static_cast<std::int64_t>(product);
  }
  if (product == negative_limit) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return -static_cast<std::int64_t>(product);
}

std::optional<std::int64_t>
checked_integer_binary(std::string_view symbol, std::int64_t left,
                       std::int64_t right) {
  if (symbol == "+") {
    return checked_add(left, right);
  }
  if (symbol == "-") {
    return checked_subtract(left, right);
  }
  if (symbol == "*") {
    return checked_multiply(left, right);
  }
  if (symbol == "/" || symbol == "//") {
    if (right == 0 ||
        (left == std::numeric_limits<std::int64_t>::min() && right == -1)) {
      return std::nullopt;
    }
    std::int64_t quotient = left / right;
    if (symbol == "//" && left % right != 0 && (left < 0) != (right < 0)) {
      --quotient;
    }
    return quotient;
  }
  return std::nullopt;
}

struct Environment {
  std::function<std::optional<Module>(std::string_view)> module;
  std::function<std::optional<Type>(const Module::TypeDecl&,
                                    std::span<const ParameterValue>)>
      type;
  std::function<std::optional<Attribute>(const Module::AttributeDecl&,
                                         std::span<const ParameterValue>)>
      attribute;
  std::function<bool(const Module::TypeDecl&,
                     const Module::InterfaceDecl&)>
      conforms;
  std::function<std::optional<ParameterValue>(
      Module::FunctionDecl, std::span<const ParameterValue>)>
      evaluate;
};

Environment environment(Compiler& compiler) {
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
          [&](Module::FunctionDecl function,
              std::span<const ParameterValue> arguments) {
            return CompilerAccess::evaluate(compiler, std::move(function),
                                            arguments);
          }};
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
  const auto conforms = [find](
                            const Module::TypeDecl& declaration,
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
        const auto imported = std::find_if(
            owner->imports().begin(), owner->imports().end(),
            [&](const Module::Import& import) {
              return import.prefix() == prefix;
            });
        module_name = imported == owner->imports().end() ? prefix
                                                         : imported->name;
      }
      if (module_name == interface.symbol().module_name() &&
          local_name == interface.name()) {
        return true;
      }
    }
    return false;
  };
  return {find,
          [modules, &diagnostics](const Module::TypeDecl& schema,
                                 std::span<const ParameterValue> parameters)
              -> std::optional<Type> {
            auto values = validate_parameters(schema.symbol().qualified_name(),
                                              schema.parameters(), parameters,
                                              diagnostics);
            if (!values) {
              return std::nullopt;
            }
            auto derived = resolve_derived_parameters(modules, schema, *values,
                                                       diagnostics);
            return derived
                       ? std::optional<Type>{TypeAccess::make(
                             schema, std::move(*values), std::move(*derived))}
                       : std::nullopt;
          },
          [&diagnostics](const Module::AttributeDecl& schema,
                         std::span<const ParameterValue> parameters)
              -> std::optional<Attribute> {
            auto values = validate_parameters(schema.symbol().qualified_name(),
                                              schema.parameters(), parameters,
                                              diagnostics);
            return values ? std::optional<Attribute>{TypeAccess::make(
                                schema, std::move(*values))}
                          : std::nullopt;
          },
          conforms,
          {}};
}

class Solver {
public:
  Solver(Environment environment, const Module::FunctionDecl& schema,
         Diagnostics& diagnostics, std::optional<SourceRange> source)
      : environment_(std::move(environment)), schema_(&schema),
        diagnostics_(diagnostics), source_(std::move(source)),
        contract_(&FunctionTypeAccess::get(schema)),
        scope_(schema.symbol().module_name()) {}

  Solver(Environment environment, const Module::TypeDecl& schema,
         Diagnostics& diagnostics)
      : environment_(std::move(environment)), diagnostics_(diagnostics),
        scope_(schema.symbol().module_name()) {}

  std::optional<OperationTypes>
  infer(std::span<const Type> operands,
        std::span<const std::optional<ParameterValue>> properties,
        std::span<const std::optional<Type>> expected) {
    if (schema_ == nullptr || contract_ == nullptr) {
      report("operation solver has no operation schema");
      return std::nullopt;
    }
    const auto static_inputs = schema_->static_inputs();
    const auto value_inputs = schema_->value_inputs();
    const auto value_results = schema_->value_results();
    if (properties.size() != static_inputs.size()) {
      report("operation property map does not match its schema");
      return std::nullopt;
    }
    Bindings bindings;
    std::size_t operand = 0;
    for (const auto& input : value_inputs) {
      const std::size_t count = input.variadic ? operands.size() - operand : 1U;
      if (operand + count > operands.size()) {
        report("operation has too few operands");
        return std::nullopt;
      }
      for (std::size_t item = 0; item < count; ++item) {
        if (!unify(input.domain, ParameterValue(operands[operand++]), bindings)) {
          return std::nullopt;
        }
      }
    }
    if (operand != operands.size()) {
      report("operation has too many operands");
      return std::nullopt;
    }
    std::size_t property = 0;
    for (std::size_t input_index = 0; input_index < schema_->inputs().size();
         ++input_index) {
      const auto& input = schema_->inputs()[input_index];
      if (input.kind != Module::ParameterDecl::Kind::Static) {
        continue;
      }
      std::optional<ParameterValue> actual = properties[property++];
      if (!actual && input.default_value) {
        actual = parameter_default(input);
      }
      if (!actual) {
        if (!contract_->bindings.empty() &&
            contract_->bindings[input_index]) {
          report("operation is missing property '" + input.name + "'");
          return std::nullopt;
        }
        continue;
      }
      if (!matches_parameter(input, *actual)) {
        report("operation property '" + input.name + "' has the wrong kind");
        return std::nullopt;
      }
      if (!contract_->bindings.empty() && contract_->bindings[input_index] &&
          !unify(*contract_->bindings[input_index], *actual, bindings)) {
        return std::nullopt;
      }
    }
    if (expected.size() != value_results.size()) {
      report("operation result count does not match its schema");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (expected[index] &&
          !unify(value_results[index].domain,
                 ParameterValue(*expected[index]),
                 bindings)) {
        return std::nullopt;
      }
    }

    const Module::ParameterDecl type_parameter{
        "result", domain_expression(ValueKind::Type), false, std::nullopt};
    OperationTypes resolved;
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
        const auto field = std::find_if(
            interface->fields().begin(), interface->fields().end(),
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
    } else if constexpr (std::is_same_v<Declaration,
                                        Module::FunctionDecl>) {
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
               "' does not implement interface '" + *generic->constraint +
               "'");
      }
      return false;
    }
    return true;
  }

  std::optional<ParameterValue>
  expression_literal(const TypeExpression& expression,
                     ValueKind expected) {
    using Kind = TypeExpression::Kind;
    if (expected == ValueKind::Integer &&
        expression.kind == Kind::Number) {
      std::int64_t value = 0;
      const auto parsed = std::from_chars(
          expression.text.data(),
          expression.text.data() + expression.text.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == expression.text.data() + expression.text.size()) {
        return ParameterValue(value);
      }
    } else if (expected == ValueKind::Real &&
               expression.kind == Kind::Number) {
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
        interface
            ? std::find_if(interface->fields().begin(), interface->fields().end(),
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
             "' does not implement interface '" + *generic.constraint +
             "'");
      return std::nullopt;
    }

    const auto derived = std::find_if(
        type->schema().derived_parameters().begin(),
        type->schema().derived_parameters().end(), [&](const auto& candidate) {
          return candidate.name == field_name;
        });
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
    const std::string identity =
        std::string(type->stable_name()) + "/derived/" +
        std::string(field_name);
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

  std::vector<Module::FunctionDecl>
  operator_declarations(std::string_view symbol,
                        Module::FunctionDecl::Fixity fixity,
                        const Module::ParameterDecl& expected,
                        std::size_t arity) {
    std::vector<Module::FunctionDecl> result;
    const auto owner = environment_.module(scope_);
    if (!owner) {
      return result;
    }
    std::vector<Module> visible{*owner};
    for (const auto& import : owner->imports()) {
      if (const auto module = environment_.module(import.name)) {
        visible.push_back(*module);
      }
    }
    for (const Module& module : visible) {
      for (const auto& candidate : module.functions()) {
        const auto static_inputs = candidate.static_inputs();
        const auto static_results = candidate.static_results();
        if (candidate.operator_symbol() != symbol ||
            candidate.operator_fixity() != fixity ||
            !candidate.value_inputs().empty() ||
            !candidate.value_results().empty() ||
            static_inputs.size() != arity || static_results.size() != 1U ||
            static_results.front().domain != expected.domain) {
          continue;
        }
        const bool inputs_match = std::all_of(
            static_inputs.begin(), static_inputs.end(),
            [&](const auto& input) { return input.domain == expected.domain; });
        if (inputs_match) {
          result.push_back(candidate);
        }
      }
    }
    return result;
  }

  std::optional<ParameterValue> evaluate(const TypeExpression& expression,
                                         const Module::ParameterDecl& expected,
                                         const Bindings& bindings) {
    using Kind = TypeExpression::Kind;
    const auto domain = kernel_domain(expected.domain);
    if (!domain) {
      report("unknown parameter domain for '" + expected.name + "'");
      return std::nullopt;
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
              *generic, std::string_view(expression.text).substr(field_dot + 1U),
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
    const bool operation = expression.kind == Kind::Prefix ||
                           expression.kind == Kind::Infix ||
                           expression.kind == Kind::Postfix;
    if (operation) {
      if (domain->element != ValueKind::Integer &&
          domain->element != ValueKind::Real) {
        report("operator expression is not defined for this compiler domain");
        return std::nullopt;
      }
      const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
      if (expression.arguments.size() != arity) {
        report("malformed operator expression");
        return std::nullopt;
      }
      const auto fixity =
          expression.kind == Kind::Prefix
              ? Module::FunctionDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix
              ? Module::FunctionDecl::Fixity::Postfix
              : Module::FunctionDecl::Fixity::Infix;
      const auto overloads =
          operator_declarations(expression.text, fixity, expected, arity);
      if (overloads.size() > 1U) {
        report("compile-time operator '" + expression.text +
               "' is ambiguous");
        return std::nullopt;
      }
      if (overloads.size() == 1U) {
        const auto function = overloads.front();
        Bindings arguments;
        std::vector<ParameterValue> values;
        values.reserve(arity);
        const auto inputs = function.static_inputs();
        for (std::size_t index = 0; index < arity; ++index) {
          auto value = evaluate(expression.arguments[index], inputs[index],
                                bindings);
          if (!value) {
            return std::nullopt;
          }
          values.push_back(*value);
          arguments.emplace(inputs[index].name, std::move(*value));
        }
        const std::string identity = function.symbol().stable_name();
        if (std::find(calls_.begin(), calls_.end(), identity) != calls_.end()) {
          report("recursive compile-time operator '" + expression.text + "'");
          return std::nullopt;
        }
        calls_.push_back(identity);
        std::optional<ParameterValue> value;
        if (ModuleAccess::expression(function) != nullptr) {
          const std::string caller_scope = scope_;
          scope_ = std::string(function.symbol().module_name());
          value = evaluate(*ModuleAccess::expression(function),
                           function.static_results().front(), arguments);
          scope_ = caller_scope;
        } else if (environment_.evaluate) {
          value = environment_.evaluate(function, values);
        } else {
          report("compile-time operator '" + expression.text +
                 "' has no registered evaluator");
        }
        calls_.pop_back();
        return value;
      }
      auto left = evaluate(expression.arguments.front(), expected, bindings);
      if (!left) {
        return std::nullopt;
      }
      if (expression.kind != Kind::Infix) {
        if (expression.kind != Kind::Prefix || expression.text != "-") {
          report("no matching compile-time operator '" + expression.text +
                 "'");
          return std::nullopt;
        }
        if (const auto* integer = left->as_i64()) {
          if (*integer == std::numeric_limits<std::int64_t>::min()) {
            report("compile-time integer arithmetic overflow");
            return std::nullopt;
          }
          return ParameterValue(-*integer);
        }
        if (const auto* real = left->as_f64()) {
          const double value = -*real;
          if (std::isfinite(value)) {
            return ParameterValue(value);
          }
        }
        report("compile-time floating-point arithmetic is not finite");
        return std::nullopt;
      }
      auto right = evaluate(expression.arguments[1], expected, bindings);
      if (!right) {
        return std::nullopt;
      }
      if (const auto* left_integer = left->as_i64()) {
        const auto* right_integer = right->as_i64();
        const auto value =
            right_integer
                ? checked_integer_binary(expression.text, *left_integer,
                                         *right_integer)
                : std::nullopt;
        if (value) {
          return ParameterValue(*value);
        }
        report((expression.text == "/" || expression.text == "//") &&
                       right_integer &&
                       *right_integer == 0
                   ? "compile-time division by zero"
                   : "compile-time integer arithmetic overflow");
        return std::nullopt;
      }
      const auto* left_real = left->as_f64();
      const auto* right_real = right->as_f64();
      if (left_real == nullptr || right_real == nullptr ||
          ((expression.text == "/" || expression.text == "//") &&
           *right_real == 0.0)) {
        report((expression.text == "/" || expression.text == "//") &&
                       right_real &&
                       *right_real == 0.0
                   ? "compile-time division by zero"
                   : "compile-time arithmetic operands have different kinds");
        return std::nullopt;
      }
      const double value = expression.text == "+"   ? *left_real + *right_real
                           : expression.text == "-" ? *left_real - *right_real
                           : expression.text == "*" ? *left_real * *right_real
                           : expression.text == "/" ? *left_real / *right_real
                           : expression.text == "//"
                               ? std::floor(*left_real / *right_real)
                               : std::numeric_limits<double>::quiet_NaN();
      if (!std::isfinite(value)) {
        report("compile-time floating-point arithmetic is not finite");
        return std::nullopt;
      }
      return ParameterValue(value);
    }
    if (expression.kind == Kind::Call) {
      const bool kernel_call =
          expression.text == "ceildiv" || expression.text == "min" ||
          expression.text == "max";
      if (kernel_call) {
        if (domain->element != ValueKind::Integer ||
            expression.arguments.size() != 2U) {
          report("ill-typed kernel call '" + expression.text + "'");
          return std::nullopt;
        }
        auto left = evaluate(expression.arguments[0], expected, bindings);
        auto right = evaluate(expression.arguments[1], expected, bindings);
        const auto* left_integer = left ? left->as_i64() : nullptr;
        const auto* right_integer = right ? right->as_i64() : nullptr;
        if (left_integer == nullptr || right_integer == nullptr) {
          return std::nullopt;
        }
        if (expression.text == "min") {
          return ParameterValue(std::min(*left_integer, *right_integer));
        }
        if (expression.text == "max") {
          return ParameterValue(std::max(*left_integer, *right_integer));
        }
        if (*left_integer < 0 || *right_integer <= 0) {
          report(
              "ceildiv requires a non-negative dividend and positive divisor");
          return std::nullopt;
        }
        return ParameterValue(*left_integer / *right_integer +
                              (*left_integer % *right_integer != 0 ? 1 : 0));
      }

      auto function = declaration<Module::FunctionDecl>(expression.text);
      if (!function || !function->value_inputs().empty() ||
          !function->value_results().empty() ||
          function->static_results().size() != 1U ||
          function->static_results().front().domain != expected.domain ||
          function->static_inputs().size() != expression.arguments.size()) {
        if (function) {
          report("ill-typed const call '" + expression.text + "'");
        }
        return std::nullopt;
      }
      const auto inputs = function->static_inputs();
      Bindings arguments;
      std::vector<ParameterValue> values;
      values.reserve(expression.arguments.size());
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        auto value = evaluate(expression.arguments[index],
                              inputs[index], bindings);
        if (!value) {
          return std::nullopt;
        }
        values.push_back(*value);
        arguments.emplace(inputs[index].name, std::move(*value));
      }
      const std::string identity = function->symbol().stable_name();
      if (std::find(calls_.begin(), calls_.end(), identity) != calls_.end()) {
        report("recursive pure function call '" + expression.text + "'");
        return std::nullopt;
      }
      calls_.push_back(identity);
      std::optional<ParameterValue> value;
      if (ModuleAccess::expression(*function) != nullptr) {
        const std::string caller_scope = scope_;
        scope_ = std::string(function->symbol().module_name());
        value = evaluate(*ModuleAccess::expression(*function),
                         function->static_results().front(), arguments);
        scope_ = caller_scope;
      } else if (environment_.evaluate) {
        value = environment_.evaluate(*function, values);
      } else {
        report("compile-time call '" + expression.text +
               "' has no registered evaluator");
      }
      calls_.pop_back();
      return value;
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
            ? std::find_if(contract_ ? contract_->generics.begin()
                                     : empty_generics_.begin(),
                           contract_ ? contract_->generics.end()
                                     : empty_generics_.end(),
                           [&](const auto& candidate) {
                             return candidate.name == std::string_view(
                                 expression.text.data(), field_dot);
                           })
            : (contract_ ? contract_->generics.end() : empty_generics_.end());
    const auto generic_end =
        contract_ ? contract_->generics.end() : empty_generics_.end();
    const bool computed = field_generic != generic_end ||
                          expression.kind == Kind::Call ||
                          expression.kind == Kind::If ||
                          expression.kind == Kind::Evaluate ||
                          expression.kind == Kind::Prefix ||
                          expression.kind == Kind::Infix ||
                          expression.kind == Kind::Postfix;
    if (computed) {
      Module::ParameterDecl expected{
          "computed", domain_expression(ValueKind::Integer), false,
          std::nullopt};
      const bool kernel_call = expression.kind == Kind::Call &&
                               (expression.text == "ceildiv" ||
                                expression.text == "min" ||
                                expression.text == "max");
      if (field_generic != generic_end) {
        const auto interface =
            field_generic->constraint
                ? interface_declaration(*field_generic->constraint)
                : std::optional<Module::InterfaceDecl>{};
        const std::string_view field_name =
            std::string_view(expression.text).substr(field_dot + 1U);
        const auto field =
            interface
                ? std::find_if(interface->fields().begin(),
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
      } else if (expression.kind == Kind::Call && !kernel_call) {
        const std::size_t receiver_dot = expression.text.find('.');
        const auto generic =
            receiver_dot == std::string::npos
                ? (contract_ ? contract_->generics.end()
                             : empty_generics_.end())
                : std::find_if(
                      contract_ ? contract_->generics.begin()
                                : empty_generics_.begin(),
                      contract_ ? contract_->generics.end()
                                : empty_generics_.end(),
                      [&](const auto& candidate) {
                        return candidate.name == std::string_view(
                                                     expression.text.data(),
                                                     receiver_dot);
                      });
        if (generic == generic_end) {
          auto function = declaration<Module::FunctionDecl>(expression.text);
          if (!function || ModuleAccess::expression(*function) == nullptr ||
              !function->value_inputs().empty() ||
              !function->value_results().empty() ||
              function->static_results().size() != 1U) {
            return false;
          }
          expected = function->static_results().front();
          if (!matches_parameter(expected, actual)) {
            report("const function result does not match the type parameter");
            return false;
          }
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

  Environment environment_;
  const Module::FunctionDecl* schema_ = nullptr;
  Diagnostics& diagnostics_;
  std::optional<SourceRange> source_;
  const FunctionTypeContract* contract_ = nullptr;
  const std::vector<GenericDefinition> empty_generics_;
  std::string scope_;
  std::vector<std::string> calls_;
};

}  // namespace

std::optional<std::vector<Type>>
infer_operation_types(Compiler& compiler, const Module::FunctionDecl& schema,
                      std::span<const Type> operands,
                      std::span<const std::optional<ParameterValue>> properties,
                      std::span<const std::optional<Type>> expected_results,
                      Diagnostics& diagnostics,
                      std::optional<SourceRange> source) {
  auto resolved = resolve_operation_types(compiler, schema, operands,
                                          properties, expected_results,
                                          diagnostics, std::move(source));
  return resolved ? std::optional<std::vector<Type>>{
                        std::move(resolved->results)}
                  : std::nullopt;
}

std::optional<std::vector<Type>> infer_operation_types(
    std::span<const Module> modules, const Module::FunctionDecl& schema,
    std::span<const Type> operands,
    std::span<const std::optional<ParameterValue>> properties,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source) {
  auto resolved = resolve_operation_types(modules, schema, operands,
                                          properties, expected_results,
                                          diagnostics, std::move(source));
  return resolved ? std::optional<std::vector<Type>>{
                        std::move(resolved->results)}
                  : std::nullopt;
}

std::optional<OperationTypes>
resolve_operation_types(Compiler& compiler,
                        const Module::FunctionDecl& schema,
                        std::span<const Type> operands,
                        std::span<const std::optional<ParameterValue>> properties,
                        std::span<const std::optional<Type>> expected_results,
                        Diagnostics& diagnostics,
                        std::optional<SourceRange> source) {
  return Solver(environment(compiler), schema, diagnostics, std::move(source))
      .infer(operands, properties, expected_results);
}

std::optional<OperationTypes> resolve_operation_types(
    std::span<const Module> modules, const Module::FunctionDecl& schema,
    std::span<const Type> operands,
    std::span<const std::optional<ParameterValue>> properties,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source) {
  return Solver(environment(modules, diagnostics), schema, diagnostics,
                std::move(source))
      .infer(operands, properties, expected_results);
}

std::optional<std::vector<ParameterValue>> resolve_derived_parameters(
    Compiler& compiler, const Module::TypeDecl& schema,
    std::span<const ParameterValue> parameters, Diagnostics& diagnostics) {
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
