#include "type_contract.h"

#include "compiler_internal.h"
#include "module_internal.h"
#include "type_internal.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <locale>
#include <sstream>
#include <string>
#include <unordered_map>

namespace joggle::detail {
namespace {

using Bindings = std::unordered_map<std::string, ParameterValue>;

struct Environment {
  std::function<std::optional<Module>(std::string_view)> module;
  std::function<std::optional<Type>(const Module::TypeDecl&,
                                    std::span<const ParameterValue>)>
      type;
  std::function<std::optional<Attribute>(const Module::AttributeDecl&,
                                         std::span<const ParameterValue>)>
      attribute;
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
  return {find,
          [&diagnostics](const Module::TypeDecl& schema,
                         std::span<const ParameterValue> parameters)
              -> std::optional<Type> {
            auto values = validate_parameters(schema.symbol().qualified_name(),
                                              schema.parameters(), parameters,
                                              diagnostics);
            return values ? std::optional<Type>{TypeAccess::make(
                                schema, std::move(*values))}
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
          }};
}

class Solver {
public:
  Solver(Environment environment, const Module::OperationDecl& schema,
         Diagnostics& diagnostics, std::optional<SourceRange> source)
      : environment_(std::move(environment)), schema_(schema),
        diagnostics_(diagnostics), source_(std::move(source)),
        contract_(OperationTypeAccess::get(schema)),
        scope_(schema.symbol().module_name()) {}

  std::optional<std::vector<Type>>
  infer(std::span<const Type> operands,
        std::span<const std::optional<ParameterValue>> properties,
        std::span<const std::optional<Type>> expected) {
    if (properties.size() != schema_.inputs().size()) {
      report("operation property map does not match its schema");
      return std::nullopt;
    }
    Bindings bindings;
    std::size_t operand = 0;
    for (std::size_t index = 0; index < schema_.inputs().size(); ++index) {
      const auto& input = schema_.inputs()[index];
      if (input.kind != Module::ParameterKind::Value) {
        continue;
      }
      if (index >= contract_.inputs.size() || !contract_.inputs[index]) {
        report("operation schema has an untyped operand");
        return std::nullopt;
      }
      const std::size_t count = input.variadic ? operands.size() - operand : 1U;
      if (operand + count > operands.size()) {
        report("operation has too few operands");
        return std::nullopt;
      }
      for (std::size_t item = 0; item < count; ++item) {
        if (!unify(*contract_.inputs[index],
                   ParameterValue(operands[operand++]), bindings)) {
          return std::nullopt;
        }
      }
    }
    if (operand != operands.size()) {
      report("operation has too many operands");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < schema_.inputs().size(); ++index) {
      const auto& input = schema_.inputs()[index];
      if (input.kind == Module::ParameterKind::Value ||
          input.kind == Module::ParameterKind::Region) {
        continue;
      }
      std::optional<ParameterValue> actual = properties[index];
      if (!actual && input.default_value) {
        actual = literal_value(*input.default_value);
      }
      if (!actual) {
        if (contract_.inputs[index]) {
          report("operation is missing property '" + input.name + "'");
          return std::nullopt;
        }
        continue;
      }
      if (!matches_parameter(input, *actual)) {
        report("operation property '" + input.name + "' has the wrong kind");
        return std::nullopt;
      }
      if (contract_.inputs[index] &&
          !unify(*contract_.inputs[index], *actual, bindings)) {
        return std::nullopt;
      }
    }
    if (expected.size() != contract_.results.size()) {
      report("operation result count does not match its schema");
      return std::nullopt;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
      if (expected[index] &&
          !unify(contract_.results[index], ParameterValue(*expected[index]),
                 bindings)) {
        return std::nullopt;
      }
    }

    const Module::ParameterDecl type_parameter{
        "result", Module::ParameterKind::Type, false, false, std::nullopt};
    std::vector<Type> results;
    results.reserve(contract_.results.size());
    for (const auto& expression : contract_.results) {
      auto value = evaluate(expression, type_parameter, bindings);
      if (!value || value->as_type() == nullptr) {
        return std::nullopt;
      }
      results.push_back(*value->as_type());
    }
    return results;
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
    } else {
      result = module->attribute(local);
    }
    if (!result) {
      report("type contract references unknown declaration '" +
             std::string(reference) + "'");
    }
    return result;
  }

  static ParameterValue literal_value(const Module::Literal& literal) {
    return std::visit([](const auto& value) { return ParameterValue(value); },
                      literal);
  }

  std::optional<ParameterValue>
  expression_literal(const TypeExpression& expression,
                     Module::ParameterKind expected) {
    using Kind = TypeExpression::Kind;
    if (expected == Module::ParameterKind::I64 &&
        expression.kind == Kind::Number) {
      std::int64_t value = 0;
      const auto parsed = std::from_chars(
          expression.text.data(),
          expression.text.data() + expression.text.size(), value);
      if (parsed.ec == std::errc{} &&
          parsed.ptr == expression.text.data() + expression.text.size()) {
        return ParameterValue(value);
      }
    } else if (expected == Module::ParameterKind::F64 &&
               expression.kind == Kind::Number) {
      double value = 0.0;
      std::istringstream input(expression.text);
      input.imbue(std::locale::classic());
      input >> value;
      if (input && input.peek() == std::char_traits<char>::eof()) {
        return ParameterValue(value);
      }
    } else if (expected == Module::ParameterKind::Boolean &&
               expression.kind == Kind::Boolean) {
      return ParameterValue(expression.text == "true");
    } else if (expected == Module::ParameterKind::String &&
               expression.kind == Kind::String) {
      return ParameterValue(expression.text);
    }
    report("type expression literal has the wrong kind");
    return std::nullopt;
  }

  std::optional<ParameterValue> evaluate(const TypeExpression& expression,
                                         const Module::ParameterDecl& expected,
                                         const Bindings& bindings) {
    using Kind = TypeExpression::Kind;
    if (expression.kind == Kind::Variable) {
      const auto found = bindings.find(expression.text);
      if (found == bindings.end()) {
        report("cannot infer type variable '" + expression.text + "'");
        return std::nullopt;
      }
      return found->second;
    }
    if (expected.list) {
      if (expression.kind != Kind::List) {
        report("expected a list-valued type expression");
        return std::nullopt;
      }
      Module::ParameterDecl element = expected;
      element.list = false;
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
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      return expression_literal(expression, expected.kind);
    }
    if (expression.kind != Kind::Reference) {
      report("invalid type expression");
      return std::nullopt;
    }
    if (expected.kind == Module::ParameterKind::Type) {
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
    if (expected.kind == Module::ParameterKind::Attribute) {
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
    return expression_literal(expression, expected.kind);
  }

  bool unify(const TypeExpression& expression, const ParameterValue& actual,
             Bindings& bindings) {
    using Kind = TypeExpression::Kind;
    if (expression.kind == Kind::Variable) {
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
    if (expression.kind == Kind::Number || expression.kind == Kind::Boolean ||
        expression.kind == Kind::String) {
      Module::ParameterKind expected = Module::ParameterKind::I64;
      switch (actual.kind()) {
      case ParameterValue::Kind::I64:
        expected = Module::ParameterKind::I64;
        break;
      case ParameterValue::Kind::F64:
        expected = Module::ParameterKind::F64;
        break;
      case ParameterValue::Kind::Boolean:
        expected = Module::ParameterKind::Boolean;
        break;
      case ParameterValue::Kind::String:
        expected = Module::ParameterKind::String;
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
            literal_value(*target->parameters()[index].default_value) !=
                parameters[index]) {
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
  const Module::OperationDecl& schema_;
  Diagnostics& diagnostics_;
  std::optional<SourceRange> source_;
  const OperationTypeContract& contract_;
  std::string scope_;
};

}  // namespace

std::optional<std::vector<Type>>
infer_operation_types(Compiler& compiler, const Module::OperationDecl& schema,
                      std::span<const Type> operands,
                      std::span<const std::optional<ParameterValue>> properties,
                      std::span<const std::optional<Type>> expected_results,
                      Diagnostics& diagnostics,
                      std::optional<SourceRange> source) {
  return Solver(environment(compiler), schema, diagnostics, std::move(source))
      .infer(operands, properties, expected_results);
}

std::optional<std::vector<Type>> infer_operation_types(
    std::span<const Module> modules, const Module::OperationDecl& schema,
    std::span<const Type> operands,
    std::span<const std::optional<ParameterValue>> properties,
    std::span<const std::optional<Type>> expected_results,
    Diagnostics& diagnostics, std::optional<SourceRange> source) {
  return Solver(environment(modules, diagnostics), schema, diagnostics,
                std::move(source))
      .infer(operands, properties, expected_results);
}

}  // namespace joggle::detail
