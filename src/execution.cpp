#include "execution.h"

#include "call_resolution.h"
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
      auto shaped = call_candidate(declaration, expression);
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

bool verify_body_calls(Compiler& compiler,
                       const Module::FunctionDecl& function,
                       const FunctionBody& body, Diagnostics& diagnostics) {
  const std::size_t before = diagnostics.size();
  const auto report = [&](std::string message, SyntaxRange range) {
    diagnostics.report(
        std::move(message),
        SourceRange{body.source, range.begin, range.end});
  };
  const auto verify_expression = [&](const auto& self,
                                     const ExpressionSyntax& syntax) -> void {
    using Kind = Module::Expression::Kind;
    const Module::Expression& expression = syntax.value;
    if (expression.kind == Kind::Call) {
      const bool bootstrap = is_bootstrap_call(expression.text);
      const auto declarations = visible_functions(
          compiler, function.symbol().module_name(), expression.text);
      const bool shaped = std::any_of(
          declarations.begin(), declarations.end(), [&](const auto& current) {
            return call_candidate(current, expression).has_value();
          });
      if (!bootstrap && !shaped) {
        report("no visible overload of '" + expression.text +
                   "' accepts this call shape",
               syntax.range);
      }
    } else if (expression.kind == Kind::Prefix ||
               expression.kind == Kind::Infix ||
               expression.kind == Kind::Postfix) {
      const auto fixity =
          expression.kind == Kind::Prefix
              ? Module::FunctionDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix
              ? Module::FunctionDecl::Fixity::Postfix
              : Module::FunctionDecl::Fixity::Infix;
      const auto declarations = visible_operators(
          compiler, function.symbol().module_name(), expression.text, fixity);
      const bool shaped = std::any_of(
          declarations.begin(), declarations.end(), [&](const auto& current) {
            return call_candidate(current, expression).has_value();
          });
      const bool bootstrap_operator =
          is_bootstrap_operator(expression.text);
      if ((!declarations.empty() && !shaped) ||
          (declarations.empty() && !bootstrap_operator)) {
        report("no visible function defines operator '" + expression.text +
                   "' with this fixity and arity",
               syntax.range);
      }
    }
    for (const auto& argument : expression.arguments) {
      self(self, ExpressionSyntax{argument, syntax.range});
    }
  };
  const auto verify_statements = [&](const auto& self,
                                     std::span<const StatementSyntax> code)
      -> void {
    for (const StatementSyntax& statement : code) {
      if (statement.kind == StatementSyntax::Kind::Expression ||
          statement.kind == StatementSyntax::Kind::If ||
          statement.kind == StatementSyntax::Kind::While) {
        verify_expression(verify_expression, statement.expression);
      }
      for (const auto& value : statement.values) {
        verify_expression(verify_expression, value);
      }
      self(self, statement.body);
      self(self, statement.otherwise);
    }
  };
  for (const BlockSyntax& block : body.blocks) {
    verify_statements(verify_statements, block.statements);
    if (!block.terminator) {
      continue;
    }
    if (block.terminator->condition) {
      verify_expression(verify_expression, *block.terminator->condition);
    }
    for (const auto& value : block.terminator->values) {
      verify_expression(verify_expression, value);
    }
    for (const auto& successor : block.terminator->successors) {
      for (const auto& argument : successor.arguments) {
        verify_expression(verify_expression, argument);
      }
    }
  }
  return diagnostics.size() == before;
}

}  // namespace joggle::detail
