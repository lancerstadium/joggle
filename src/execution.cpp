#include "execution.h"

#include "compiler_internal.h"
#include "function_body.h"
#include "module_internal.h"
#include "type_internal.h"
#include "type_contract.h"

#include <algorithm>
#include <charconv>
#include <locale>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace joggle::detail {
namespace {

template <typename T>
std::optional<ExecutionValue> list_execution_value(
    const ParameterValue& value) {
  auto decoded = decode_parameter<std::vector<T>>(value);
  return decoded ? std::optional<ExecutionValue>{std::move(*decoded)}
                 : std::nullopt;
}

template <typename T>
ParameterValue list_parameter_value(const std::vector<T>& values) {
  std::vector<ParameterValue> elements;
  elements.reserve(values.size());
  for (const T& value : values) {
    elements.emplace_back(value);
  }
  return ParameterValue::list(std::move(elements));
}

ParameterValue list_parameter_value(const std::vector<bool>& values) {
  std::vector<ParameterValue> elements;
  elements.reserve(values.size());
  for (const bool value : values) {
    elements.emplace_back(value);
  }
  return ParameterValue::list(std::move(elements));
}

std::vector<Module::FunctionDecl>
visible_functions(Compiler& compiler, std::string_view owner,
                  std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string module_name(owner);
  std::string_view local = reference;
  if (dot != std::string_view::npos) {
    const std::string_view prefix = reference.substr(0U, dot);
    local = reference.substr(dot + 1U);
    if (prefix != owner) {
      const auto scope = compiler.module(owner);
      const auto imported =
          scope ? std::find_if(scope->imports().begin(), scope->imports().end(),
                               [&](const Module::Import& import) {
                                 return import.prefix() == prefix;
                               })
                : std::span<const Module::Import>::iterator{};
      if (!scope || imported == scope->imports().end()) {
        return {};
      }
      module_name = imported->name;
    }
  }
  const auto module = compiler.module(module_name);
  if (!module) {
    return {};
  }
  return module->overloads(local);
}

std::vector<Module::FunctionDecl>
visible_operators(Compiler& compiler, std::string_view owner,
                  std::string_view symbol,
                  Module::FunctionDecl::Fixity fixity) {
  std::vector<Module::FunctionDecl> result;
  const auto scope = compiler.module(owner);
  if (!scope) {
    return result;
  }
  const auto append = [&](const Module& module) {
    for (const auto& function : module.functions()) {
      if (function.operator_symbol() == symbol &&
          function.operator_fixity() == fixity) {
        result.push_back(function);
      }
    }
  };
  append(*scope);
  for (const auto& import : scope->imports()) {
    if (const auto module = compiler.module(import.name)) {
      append(*module);
    }
  }
  return result;
}

struct CallCandidate {
  Module::FunctionDecl function;
  std::vector<std::size_t> parameters;
};

std::optional<CallCandidate>
candidate(const Module::FunctionDecl& function,
          const Module::Expression& expression) {
  const auto parameters = function.inputs();
  if (std::any_of(parameters.begin(), parameters.end(),
                  [](const Module::ParameterDecl& parameter) {
                    return parameter.variadic;
                  })) {
    return std::nullopt;
  }
  CallCandidate result{function, {}};
  result.parameters.reserve(expression.arguments.size());
  std::vector<bool> supplied(parameters.size(), false);
  std::size_t positional = 0;
  for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
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
    } else {
      while (positional < parameters.size() && supplied[positional]) {
        ++positional;
      }
      if (positional < parameters.size()) {
        target = positional++;
      }
    }
    if (target == parameters.size() || supplied[target]) {
      return std::nullopt;
    }
    supplied[target] = true;
    result.parameters.push_back(target);
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!supplied[index] && !parameters[index].default_value) {
      return std::nullopt;
    }
  }
  return result;
}

class BodyEvaluator {
  enum class Control { Next, Return, Break, Continue, Error };

  struct Flow {
    Control control = Control::Next;
    std::optional<ExecutionValue> value;
  };

  using Scope = std::unordered_map<std::string, ExecutionValue>;
  using Scopes = std::vector<Scope>;

public:
  BodyEvaluator(Compiler& compiler, const Module::FunctionDecl& function,
                const FunctionBody& body,
                std::span<const ExecutionValue> arguments,
                Compiler::EvaluationLimits limits, std::size_t& steps,
                bool under_residual_control, Diagnostics& diagnostics,
                const ExecuteFunction& execute)
      : compiler_(compiler), function_(function), body_(body), limits_(limits),
        steps_(steps), under_residual_control_(under_residual_control),
        diagnostics_(diagnostics), execute_(execute), scopes_(1U) {
    for (std::size_t index = 0; index < function_.inputs().size(); ++index) {
      scopes_.front().emplace(function_.inputs()[index].name,
                              arguments[index]);
    }
  }

  std::optional<ExecutionValue> run() {
    Flow flow = sequence(body_.blocks.front().statements);
    if (flow.control != Control::Return || !flow.value) {
      if (flow.control != Control::Error) {
        report("compiler function path falls through without returning",
               body_.range);
      }
      return std::nullopt;
    }
    return flow.value;
  }

private:
  void report(std::string message, SyntaxRange range) {
    diagnostics_.report(
        std::move(message),
        SourceRange{body_.source, range.begin, range.end});
  }

  bool step(SyntaxRange range) {
    if (steps_++ < limits_.steps) {
      return true;
    }
    report("compiler execution step limit exceeded", range);
    return false;
  }

  ExecutionValue* local(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->find(std::string(name));
      if (found != scope->end()) {
        return &found->second;
      }
    }
    return nullptr;
  }

  std::optional<ExecutionValue>
  list(const Module::Expression& expression, SyntaxRange range,
       const Module::ParameterDecl* expected) {
    std::vector<ExecutionValue> elements;
    elements.reserve(expression.arguments.size());
    for (const auto& element : expression.arguments) {
      auto value = evaluate(element, range, nullptr);
      if (!value) {
        return std::nullopt;
      }
      elements.push_back(std::move(*value));
    }
    const auto domain = expected ? kernel_domain(expected->domain)
                                 : std::optional<Domain>{};
    const std::string_view element_type = [&]() -> std::string_view {
      if (!elements.empty()) {
        return execution_value_type(elements.front());
      }
      if (!domain || !domain->list) {
        return {};
      }
      switch (domain->element) {
      case ValueKind::Integer:
        return typeid(std::int64_t).name();
      case ValueKind::Real:
        return typeid(double).name();
      case ValueKind::Boolean:
        return typeid(bool).name();
      case ValueKind::String:
        return typeid(std::string).name();
      case ValueKind::Type:
        return typeid(Type).name();
      case ValueKind::Attribute:
        return typeid(Attribute).name();
      case ValueKind::Bytes:
      case ValueKind::Function:
        return {};
      }
      return {};
    }();
    if (element_type.empty() ||
        !std::all_of(elements.begin(), elements.end(),
                     [&](const ExecutionValue& element) {
                       return execution_value_type(element) == element_type;
                     })) {
      report(elements.empty()
                 ? "an empty compiler list needs a contextual element type"
                 : "compiler list elements have different types",
             range);
      return std::nullopt;
    }
    if (element_type == typeid(std::int64_t).name()) {
      IntegerList result;
      for (auto& element : elements) {
        result.push_back(std::get<std::int64_t>(element));
      }
      return ExecutionValue{std::move(result)};
    }
    if (element_type == typeid(double).name()) {
      RealList result;
      for (auto& element : elements) {
        result.push_back(std::get<double>(element));
      }
      return ExecutionValue{std::move(result)};
    }
    if (element_type == typeid(bool).name()) {
      BooleanList result;
      for (auto& element : elements) {
        result.push_back(std::get<bool>(element));
      }
      return ExecutionValue{std::move(result)};
    }
    if (element_type == typeid(std::string).name()) {
      StringList result;
      for (auto& element : elements) {
        result.push_back(std::get<std::string>(std::move(element)));
      }
      return ExecutionValue{std::move(result)};
    }
    if (element_type == typeid(Type).name()) {
      TypeList result;
      for (auto& element : elements) {
        result.push_back(std::get<Type>(std::move(element)));
      }
      return ExecutionValue{std::move(result)};
    }
    if (element_type == typeid(Attribute).name()) {
      AttributeList result;
      for (auto& element : elements) {
        result.push_back(std::get<Attribute>(std::move(element)));
      }
      return ExecutionValue{std::move(result)};
    }
    report("compiler list element type is not representable", range);
    return std::nullopt;
  }

  std::optional<ExecutionValue>
  known_expression(const Module::Expression& expression, SyntaxRange range,
                   const Module::ParameterDecl& expected) {
    KnownBindings bindings;
    for (const auto& scope : scopes_) {
      for (const auto& [name, stored] : scope) {
        if (auto value = parameter_value(stored)) {
          bindings.insert_or_assign(name, std::move(*value));
        }
      }
    }
    auto value = evaluate_known_expression(
        compiler_, function_.symbol().module_name(), expression, expected,
        bindings, diagnostics_,
        SourceRange{body_.source, range.begin, range.end},
        !under_residual_control_);
    return value ? execution_value(*value, expected)
                 : std::optional<ExecutionValue>{};
  }

  std::optional<Module::ParameterDecl>
  infer_operator_result(const Module::Expression& expression) {
    if (expression.arguments.empty()) {
      return std::nullopt;
    }
    const Module::Expression& operand = expression.arguments.front();
    std::optional<Domain> domain;
    if ((operand.kind == Module::Expression::Kind::Variable ||
         operand.kind == Module::Expression::Kind::Reference) &&
        operand.arguments.empty()) {
      if (const auto* value = local(operand.text)) {
        domain = cpp_value_domain(execution_value_type(*value));
      }
    } else if (operand.kind == Module::Expression::Kind::Number) {
      domain = Domain{
          operand.text.find_first_of(".eE") == std::string::npos
              ? ValueKind::Integer
              : ValueKind::Real,
          false};
    }
    return domain && !domain->list
               ? std::optional<Module::ParameterDecl>{
                     {"operator result", domain_expression(domain->element),
                      false, std::nullopt}}
               : std::nullopt;
  }

  std::optional<ExecutionValue> call(
      const Module::Expression& expression, SyntaxRange range,
      const Module::ParameterDecl* expected,
      std::vector<Module::FunctionDecl> declarations = {}) {
    if (declarations.empty()) {
      declarations = visible_functions(
          compiler_, function_.symbol().module_name(), expression.text);
    }
    std::vector<CallCandidate> candidates;
    for (const auto& declaration : declarations) {
      auto shaped = candidate(declaration, expression);
      if (!shaped || declaration.results().size() > 1U ||
          (expected != nullptr &&
           (declaration.results().size() != 1U ||
            declaration.results().front().domain != expected->domain))) {
        continue;
      }
      candidates.push_back(std::move(*shaped));
    }
    if (candidates.empty()) {
      report("no overload of '" + expression.text +
                 "' accepts this call shape",
             range);
      return std::nullopt;
    }

    std::vector<ExecutionValue> supplied;
    supplied.reserve(expression.arguments.size());
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      const auto& first = candidates.front();
      const auto& first_parameter =
          first.function.inputs()[first.parameters[index]];
      const bool common = std::all_of(
          candidates.begin() + 1, candidates.end(),
          [&](const CallCandidate& current) {
            return current.function.inputs()[current.parameters[index]].domain ==
                   first_parameter.domain;
          });
      auto value = evaluate(expression.arguments[index], range,
                            common ? &first_parameter : nullptr);
      if (!value) {
        return std::nullopt;
      }
      supplied.push_back(std::move(*value));
    }

    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(),
            [&](const CallCandidate& current) {
              for (std::size_t index = 0; index < supplied.size(); ++index) {
                const auto& parameter =
                    current.function.inputs()[current.parameters[index]];
                if (!CompilerAccess::accepts(
                        compiler_, current.function, parameter,
                        execution_value_type(supplied[index]))) {
                  return true;
                }
              }
              return false;
            }),
        candidates.end());
    if (candidates.empty()) {
      report("no overload of '" + expression.text +
                 "' accepts the evaluated argument types",
             range);
      return std::nullopt;
    }
    if (candidates.size() != 1U) {
      std::string message = "call to '" + expression.text +
                            "' is ambiguous between";
      for (const auto& current : candidates) {
        message += " '" + current.function.symbol().qualified_name() + "'";
      }
      report(std::move(message), range);
      return std::nullopt;
    }

    const CallCandidate& selected = candidates.front();
    const auto parameters = selected.function.inputs();
    std::vector<std::optional<ExecutionValue>> bound(parameters.size());
    for (std::size_t index = 0; index < supplied.size(); ++index) {
      bound[selected.parameters[index]] = std::move(supplied[index]);
    }
    std::vector<ExecutionValue> arguments;
    arguments.reserve(parameters.size());
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (!bound[index] && parameters[index].default_value) {
        const auto value = parameter_default(parameters[index]);
        bound[index] = value ? execution_value(*value, parameters[index])
                             : std::nullopt;
      }
      if (!bound[index]) {
        report("compiler call is missing argument '" +
                   parameters[index].name + "'",
               range);
        return std::nullopt;
      }
      arguments.push_back(std::move(*bound[index]));
    }
    return execute_(selected.function, std::move(arguments));
  }

  std::optional<ExecutionValue>
  evaluate(const Module::Expression& expression, SyntaxRange range,
           const Module::ParameterDecl* expected) {
    if (!step(range)) {
      return std::nullopt;
    }
    using Kind = Module::Expression::Kind;
    if ((expression.kind == Kind::Variable ||
         expression.kind == Kind::Reference) &&
        expression.arguments.empty()) {
      if (auto* value = local(expression.text)) {
        return *value;
      }
      if (expression.kind == Kind::Variable) {
        report("compiler function '" + function_.symbol().qualified_name() +
                   "' references unknown value '" + expression.text + "'",
               range);
        return std::nullopt;
      }
    }
    if (expression.kind == Kind::Number) {
      if (expression.text.find_first_of(".eE") == std::string::npos) {
        std::int64_t integer = 0;
        const auto parsed = std::from_chars(
            expression.text.data(),
            expression.text.data() + expression.text.size(), integer);
        if (parsed.ec == std::errc{} &&
            parsed.ptr == expression.text.data() + expression.text.size()) {
          return ExecutionValue{integer};
        }
      } else {
        double real = 0.0;
        std::istringstream input(expression.text);
        input.imbue(std::locale::classic());
        input >> real;
        if (input && input.peek() == std::char_traits<char>::eof()) {
          return ExecutionValue{real};
        }
      }
      report("invalid compiler numeric literal", range);
      return std::nullopt;
    }
    if (expression.kind == Kind::Boolean) {
      return ExecutionValue{expression.text == "true"};
    }
    if (expression.kind == Kind::String) {
      return ExecutionValue{expression.text};
    }
    if (expression.kind == Kind::Evaluate) {
      if (expression.arguments.size() != 1U) {
        report("malformed compiler evaluation expression", range);
        return std::nullopt;
      }
      return evaluate(expression.arguments.front(), range, expected);
    }
    if (expression.kind == Kind::If) {
      if (expression.arguments.size() != 3U) {
        report("malformed compiler if expression", range);
        return std::nullopt;
      }
      const Module::ParameterDecl condition{
          "condition", domain_expression(ValueKind::Boolean), false,
          std::nullopt};
      auto value = evaluate(expression.arguments[0], range, &condition);
      const bool* selected = value ? std::get_if<bool>(&*value) : nullptr;
      if (selected == nullptr) {
        report("compiler if condition must be bool", range);
        return std::nullopt;
      }
      return evaluate(expression.arguments[*selected ? 1U : 2U], range,
                      expected);
    }
    if (expression.kind == Kind::List) {
      return list(expression, range, expected);
    }
    const bool operation = expression.kind == Kind::Prefix ||
                           expression.kind == Kind::Infix ||
                           expression.kind == Kind::Postfix;
    if (operation) {
      const auto fixity =
          expression.kind == Kind::Prefix
              ? Module::FunctionDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix
              ? Module::FunctionDecl::Fixity::Postfix
              : Module::FunctionDecl::Fixity::Infix;
      auto declarations = visible_operators(
          compiler_, function_.symbol().module_name(), expression.text,
          fixity);
      if (!declarations.empty()) {
        return call(expression, range, expected, std::move(declarations));
      }
      auto inferred = expected == nullptr ? infer_operator_result(expression)
                                          : std::nullopt;
      if (expected == nullptr && inferred) {
        expected = &*inferred;
      }
      if (expected == nullptr) {
        report("compiler operator needs a contextual result type", range);
        return std::nullopt;
      }
      return known_expression(expression, range, *expected);
    }
    if (expression.kind == Kind::FunctionType ||
        (expression.kind == Kind::Reference && expected != nullptr)) {
      return known_expression(expression, range, *expected);
    }
    return expression.kind == Kind::Call ? call(expression, range, expected)
                                         : unsupported(range);
  }

  std::optional<ExecutionValue> unsupported(SyntaxRange range) {
    report("compiler function '" + function_.symbol().qualified_name() +
               "' contains an unsupported expression",
           range);
    return std::nullopt;
  }

  Flow sequence(std::span<const StatementSyntax> code) {
    for (const StatementSyntax& statement : code) {
      if (!step(statement.range)) {
        return {Control::Error, std::nullopt};
      }
      if (statement.kind == StatementSyntax::Kind::Return) {
        if (statement.values.size() != function_.results().size() ||
            statement.values.size() > 1U) {
          report("compiler return does not match its function signature",
                 statement.range);
          return {Control::Error, std::nullopt};
        }
        if (statement.values.empty()) {
          return {Control::Return, ExecutionValue{}};
        }
        auto value = evaluate(statement.values.front().value,
                              statement.values.front().range,
                              &function_.results().front());
        return value ? Flow{Control::Return, std::move(value)}
                     : Flow{Control::Error, std::nullopt};
      }
      if (statement.kind == StatementSyntax::Kind::Break) {
        return {Control::Break, std::nullopt};
      }
      if (statement.kind == StatementSyntax::Kind::Continue) {
        return {Control::Continue, std::nullopt};
      }
      if (statement.kind == StatementSyntax::Kind::If) {
        const Module::ParameterDecl condition{
            "condition", domain_expression(ValueKind::Boolean), false,
            std::nullopt};
        auto value = evaluate(statement.expression.value,
                              statement.expression.range, &condition);
        const bool* selected = value ? std::get_if<bool>(&*value) : nullptr;
        if (selected == nullptr) {
          report("compiler if condition must be bool", statement.range);
          return {Control::Error, std::nullopt};
        }
        scopes_.emplace_back();
        Flow flow = sequence(*selected ? std::span(statement.body)
                                      : std::span(statement.otherwise));
        scopes_.pop_back();
        if (flow.control != Control::Next) {
          return flow;
        }
        continue;
      }
      if (statement.kind == StatementSyntax::Kind::While) {
        while (true) {
          const Module::ParameterDecl condition{
              "condition", domain_expression(ValueKind::Boolean), false,
              std::nullopt};
          auto value = evaluate(statement.expression.value,
                                statement.expression.range, &condition);
          const bool* selected = value ? std::get_if<bool>(&*value) : nullptr;
          if (selected == nullptr) {
            report("compiler while condition must be bool", statement.range);
            return {Control::Error, std::nullopt};
          }
          if (!*selected) {
            break;
          }
          scopes_.emplace_back();
          Flow flow = sequence(statement.body);
          scopes_.pop_back();
          if (flow.control == Control::Return ||
              flow.control == Control::Error) {
            return flow;
          }
          if (flow.control == Control::Break) {
            break;
          }
        }
        continue;
      }
      if (statement.bindings.size() > 1U) {
        report("compiler execution currently supports one call result",
               statement.range);
        return {Control::Error, std::nullopt};
      }
      auto value = evaluate(statement.expression.value,
                            statement.expression.range, nullptr);
      if (!value) {
        return {Control::Error, std::nullopt};
      }
      if (statement.bindings.empty()) {
        continue;
      }
      const BindingSyntax& binding = statement.bindings.front();
      if (binding.rebind) {
        auto* target = local(binding.name);
        if (target == nullptr) {
          report("cannot rebind unknown compiler value '" + binding.name +
                     "'",
                 binding.range);
          return {Control::Error, std::nullopt};
        }
        *target = std::move(*value);
      } else if (!scopes_.back()
                      .emplace(binding.name, std::move(*value))
                      .second) {
        report("compiler value '" + binding.name +
                   "' is already defined in this scope",
               binding.range);
        return {Control::Error, std::nullopt};
      }
    }
    return {};
  }

  Compiler& compiler_;
  Module::FunctionDecl function_;
  const FunctionBody& body_;
  Compiler::EvaluationLimits limits_;
  std::size_t& steps_;
  bool under_residual_control_ = false;
  Diagnostics& diagnostics_;
  const ExecuteFunction& execute_;
  Scopes scopes_;
};

}  // namespace

std::string_view execution_value_type(const ExecutionValue& value) {
  if (std::holds_alternative<std::int64_t>(value)) {
    return typeid(std::int64_t).name();
  }
  if (std::holds_alternative<double>(value)) {
    return typeid(double).name();
  }
  if (std::holds_alternative<bool>(value)) {
    return typeid(bool).name();
  }
  if (std::holds_alternative<std::string>(value)) {
    return typeid(std::string).name();
  }
  if (std::holds_alternative<Type>(value)) {
    return typeid(Type).name();
  }
  if (std::holds_alternative<Attribute>(value)) {
    return typeid(Attribute).name();
  }
  if (std::holds_alternative<Bytes>(value)) {
    return typeid(Bytes).name();
  }
  if (std::holds_alternative<std::shared_ptr<Function>>(value)) {
    return typeid(Function).name();
  }
  if (std::holds_alternative<IntegerList>(value)) {
    return typeid(IntegerList).name();
  }
  if (std::holds_alternative<RealList>(value)) {
    return typeid(RealList).name();
  }
  if (std::holds_alternative<BooleanList>(value)) {
    return typeid(BooleanList).name();
  }
  if (std::holds_alternative<StringList>(value)) {
    return typeid(StringList).name();
  }
  if (std::holds_alternative<TypeList>(value)) {
    return typeid(TypeList).name();
  }
  if (std::holds_alternative<AttributeList>(value)) {
    return typeid(AttributeList).name();
  }
  if (const auto* host = std::get_if<HostValue>(&value)) {
    return host->cpp_type;
  }
  return typeid(void).name();
}

std::optional<Domain> cpp_value_domain(std::string_view type) {
  if (type == typeid(std::int64_t).name()) {
    return Domain{ValueKind::Integer, false};
  }
  if (type == typeid(double).name()) {
    return Domain{ValueKind::Real, false};
  }
  if (type == typeid(bool).name()) {
    return Domain{ValueKind::Boolean, false};
  }
  if (type == typeid(std::string).name()) {
    return Domain{ValueKind::String, false};
  }
  if (type == typeid(Type).name()) {
    return Domain{ValueKind::Type, false};
  }
  if (type == typeid(Attribute).name()) {
    return Domain{ValueKind::Attribute, false};
  }
  if (type == typeid(Bytes).name()) {
    return Domain{ValueKind::Bytes, false};
  }
  if (type == typeid(Function).name()) {
    return Domain{ValueKind::Function, false};
  }
  if (type == typeid(IntegerList).name()) {
    return Domain{ValueKind::Integer, true};
  }
  if (type == typeid(RealList).name()) {
    return Domain{ValueKind::Real, true};
  }
  if (type == typeid(BooleanList).name()) {
    return Domain{ValueKind::Boolean, true};
  }
  if (type == typeid(StringList).name()) {
    return Domain{ValueKind::String, true};
  }
  if (type == typeid(TypeList).name()) {
    return Domain{ValueKind::Type, true};
  }
  if (type == typeid(AttributeList).name()) {
    return Domain{ValueKind::Attribute, true};
  }
  return std::nullopt;
}

std::optional<ExecutionValue>
execution_value(const ParameterValue& value,
                const Module::ParameterDecl& parameter) {
  const auto domain = kernel_domain(parameter.domain);
  if (domain && domain->list) {
    switch (domain->element) {
    case ValueKind::Integer:
      return list_execution_value<std::int64_t>(value);
    case ValueKind::Real:
      return list_execution_value<double>(value);
    case ValueKind::Boolean:
      return list_execution_value<bool>(value);
    case ValueKind::String:
      return list_execution_value<std::string>(value);
    case ValueKind::Type:
      return list_execution_value<Type>(value);
    case ValueKind::Attribute:
      return list_execution_value<Attribute>(value);
    case ValueKind::Function:
    case ValueKind::Bytes:
      return std::nullopt;
    }
  }
  switch (value.kind()) {
  case ParameterValue::Kind::I64:
    return ExecutionValue{*value.as_i64()};
  case ParameterValue::Kind::F64:
    return ExecutionValue{*value.as_f64()};
  case ParameterValue::Kind::Boolean:
    return ExecutionValue{*value.as_bool()};
  case ParameterValue::Kind::String:
    return ExecutionValue{*value.as_string()};
  case ParameterValue::Kind::Type:
    return ExecutionValue{*value.as_type()};
  case ParameterValue::Kind::Attribute:
    return ExecutionValue{*value.as_attribute()};
  case ParameterValue::Kind::List:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ParameterValue> parameter_value(const ExecutionValue& value) {
  if (const auto* stored = std::get_if<std::int64_t>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<double>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<bool>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<std::string>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<Type>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<Attribute>(&value)) {
    return ParameterValue(*stored);
  }
  if (const auto* stored = std::get_if<IntegerList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<RealList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<BooleanList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<StringList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<TypeList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<AttributeList>(&value)) {
    return list_parameter_value(*stored);
  }
  return std::nullopt;
}

std::optional<ExecutionValue> execute_body(
    Compiler& compiler, const Module::FunctionDecl& function,
    const FunctionBody& body, std::span<const ExecutionValue> arguments,
    Compiler::EvaluationLimits limits, std::size_t& steps,
    bool under_residual_control, Diagnostics& diagnostics,
    const ExecuteFunction& execute) {
  if (body.blocks.size() != 1U || body.blocks.front().terminator) {
    diagnostics.report(
        "compiler execution of function '" +
        function.symbol().qualified_name() +
        "' requires a structured body rather than explicit CFG blocks");
    return std::nullopt;
  }
  return BodyEvaluator(compiler, function, body, arguments, limits, steps,
                       under_residual_control, diagnostics, execute)
      .run();
}

}  // namespace joggle::detail
