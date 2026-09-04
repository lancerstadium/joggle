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
#include <utility>

namespace joggle::detail {
namespace {

class BodyEvaluator {
  struct Flow {
    Control control = Control::Next;
    std::vector<StagedValue> values;
  };

public:
  BodyEvaluator(Compiler& compiler, const Module::FunctionDecl& function,
                const FunctionBody& body,
                std::span<const ExecutionValue> arguments,
                Compiler::EvaluationLimits limits, std::size_t& steps,
                bool under_residual_control, Diagnostics& diagnostics,
                const ExecuteFunction& execute)
      : compiler_(compiler), function_(function), body_(body), limits_(limits),
        steps_(steps), under_residual_control_(under_residual_control),
        diagnostics_(diagnostics), execute_(execute) {
    locals_.push();
    const auto& contract = FunctionTypeAccess::get(function_);
    for (std::size_t index = 0; index < function_.inputs().size(); ++index) {
      auto value = stage(compiler_, arguments[index]);
      if (!value) {
        report("compiler function argument has no resolved Joggle type",
               body_.range);
        continue;
      }
      const auto& parameter = function_.inputs()[index];
      locals_.define(parameter.name, *value);
      if (index < contract.bindings.size() && contract.bindings[index] &&
          contract.bindings[index]->kind ==
              Module::Expression::Kind::Variable) {
        const std::string& generic = contract.bindings[index]->text;
        if (generic != parameter.name) {
          if (const auto* existing = locals_.find(generic)) {
            if (!same_staged_value(*existing, *value)) {
              report("generic '" + generic +
                         "' is bound to different compiler values",
                     body_.range);
            }
          } else {
            locals_.define(generic, *value);
          }
        }
      }
    }
  }

  std::optional<ExecutionValues> run() {
    Flow flow = sequence(body_.blocks.front().statements);
    if (flow.control != Control::Return ||
        flow.values.size() != function_.results().size()) {
      if (flow.control != Control::Error) {
        report("compiler function path falls through without returning",
               body_.range);
      }
      return std::nullopt;
    }
    ExecutionValues results;
    results.reserve(flow.values.size());
    for (const StagedValue& staged : flow.values) {
      const ExecutionValue* value = staged.known_value();
      if (value == nullptr) {
        report("compiler function returned a Residual value", body_.range);
        return std::nullopt;
      }
      results.push_back(*value);
    }
    return results;
  }

private:
  void report(std::string message, SyntaxRange range) {
    diagnostics_.report(std::move(message),
                        SourceRange{body_.source, range.begin, range.end});
  }

  bool step(SyntaxRange range) {
    if (steps_++ < limits_.steps) {
      return true;
    }
    report("compiler execution step limit exceeded", range);
    return false;
  }

  StagedValue* local(std::string_view name) { return locals_.find(name); }

  std::optional<StagedValue> known(ExecutionValue value, SyntaxRange range) {
    auto result = stage(compiler_, std::move(value));
    if (!result) {
      report("compiler value has no resolved Joggle type", range);
    }
    return result;
  }

  std::optional<StagedValue> list(const Module::Expression& expression,
                                  SyntaxRange range,
                                  const Module::ParameterDecl* expected) {
    std::vector<ExecutionValue> elements;
    elements.reserve(expression.arguments.size());
    for (const auto& element : expression.arguments) {
      auto value = evaluate(element, range, nullptr);
      if (!value) {
        return std::nullopt;
      }
      const ExecutionValue* known = value->known_value();
      if (known == nullptr) {
        report("compiler list element is Residual", range);
        return std::nullopt;
      }
      elements.push_back(*known);
    }
    const auto domain =
        expected ? kernel_domain(expected->domain) : std::optional<Domain>{};
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
      return known(ExecutionValue{std::move(result)}, range);
    }
    if (element_type == typeid(double).name()) {
      RealList result;
      for (auto& element : elements) {
        result.push_back(std::get<double>(element));
      }
      return known(ExecutionValue{std::move(result)}, range);
    }
    if (element_type == typeid(bool).name()) {
      BooleanList result;
      for (auto& element : elements) {
        result.push_back(std::get<bool>(element));
      }
      return known(ExecutionValue{std::move(result)}, range);
    }
    if (element_type == typeid(std::string).name()) {
      StringList result;
      for (auto& element : elements) {
        result.push_back(std::get<std::string>(std::move(element)));
      }
      return known(ExecutionValue{std::move(result)}, range);
    }
    if (element_type == typeid(Type).name()) {
      TypeList result;
      for (auto& element : elements) {
        result.push_back(std::get<Type>(std::move(element)));
      }
      return known(ExecutionValue{std::move(result)}, range);
    }
    report("compiler list element type is not representable", range);
    return std::nullopt;
  }

  std::optional<StagedValue>
  known_expression(const Module::Expression& expression, SyntaxRange range,
                   const Module::ParameterDecl& expected) {
    auto value = evaluate_known_expression(
        compiler_, function_.symbol().module_name(), expression, expected,
        locals_.known_bindings(), diagnostics_,
        SourceRange{body_.source, range.begin, range.end},
        !under_residual_control_);
    auto result = value ? execution_value(*value, expected)
                        : std::optional<ExecutionValue>{};
    return result ? known(std::move(*result), range)
                  : std::optional<StagedValue>{};
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
        const ExecutionValue* known = value->known_value();
        if (known != nullptr) {
          domain = cpp_value_domain(execution_value_type(*known));
        }
      }
    } else if (operand.kind == Module::Expression::Kind::Number) {
      domain = Domain{operand.text.find_first_of(".eE") == std::string::npos
                          ? ValueKind::Integer
                          : ValueKind::Real,
                      false};
    }
    return domain && !domain->list
               ? std::optional<Module::ParameterDecl>{{"operator result",
                                                       domain_expression(
                                                           domain->element),
                                                       false, std::nullopt}}
               : std::nullopt;
  }

  std::optional<std::vector<StagedValue>>
  call_values(const Module::Expression& expression, SyntaxRange range,
              std::size_t result_count,
              std::span<const Module::ParameterDecl> expected_results = {},
              std::vector<Module::FunctionDecl> declarations = {}) {
    const SourceRange call_site{body_.source, range.begin, range.end};
    auto results = execute_call(
        compiler_, function_.symbol().module_name(), expression, call_site,
        result_count, expected_results, diagnostics_,
        [&](const Module::Expression& argument,
            const Module::ParameterDecl* expected)
            -> std::optional<ExecutionValue> {
          auto value = evaluate(argument, range, expected);
          const ExecutionValue* known = value ? value->known_value() : nullptr;
          if (known == nullptr && value) {
            report("compiler call argument is Residual", range);
          }
          return known ? std::optional<ExecutionValue>{*known} : std::nullopt;
        },
        execute_, declarations);
    if (!results || results->size() != result_count) {
      return std::nullopt;
    }
    std::vector<StagedValue> staged;
    staged.reserve(results->size());
    for (auto& result : *results) {
      auto value = known(std::move(result), range);
      if (!value) {
        return std::nullopt;
      }
      staged.push_back(std::move(*value));
    }
    return staged;
  }

  std::optional<StagedValue>
  call(const Module::Expression& expression, SyntaxRange range,
       const Module::ParameterDecl* expected,
       std::vector<Module::FunctionDecl> declarations = {}) {
    const std::span<const Module::ParameterDecl> expected_results =
        expected == nullptr
            ? std::span<const Module::ParameterDecl>{}
            : std::span<const Module::ParameterDecl>{expected, 1U};
    auto values = call_values(expression, range, 1U, expected_results,
                              std::move(declarations));
    return values ? std::optional<StagedValue>{std::move(values->front())}
                  : std::nullopt;
  }

  std::optional<StagedValue> evaluate(const Module::Expression& expression,
                                      SyntaxRange range,
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
          return known(ExecutionValue{integer}, range);
        }
      } else {
        double real = 0.0;
        std::istringstream input(expression.text);
        input.imbue(std::locale::classic());
        input >> real;
        if (input && input.peek() == std::char_traits<char>::eof()) {
          return known(ExecutionValue{real}, range);
        }
      }
      report("invalid compiler numeric literal", range);
      return std::nullopt;
    }
    if (expression.kind == Kind::Boolean) {
      return known(ExecutionValue{expression.text == "true"}, range);
    }
    if (expression.kind == Kind::String) {
      return known(ExecutionValue{expression.text}, range);
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
      const auto selected = value ? known_boolean(*value) : std::nullopt;
      if (!selected) {
        report("compiler if condition must be bool", range);
        return std::nullopt;
      }
      return evaluate(expression.arguments[*selected ? 1U : 2U], range,
                      expected);
    }
    if (expression.kind == Kind::List) {
      return list(expression, range, expected);
    }
    const bool operator_expression = expression.kind == Kind::Prefix ||
                                     expression.kind == Kind::Infix ||
                                     expression.kind == Kind::Postfix;
    if (operator_expression) {
      const auto fixity = expression.kind == Kind::Prefix
                              ? Module::FunctionDecl::Fixity::Prefix
                          : expression.kind == Kind::Postfix
                              ? Module::FunctionDecl::Fixity::Postfix
                              : Module::FunctionDecl::Fixity::Infix;
      auto inferred = expected == nullptr ? infer_operator_result(expression)
                                          : std::nullopt;
      if (expected == nullptr && inferred) {
        expected = &*inferred;
      }
      auto declarations = visible_operators(
          compiler_, function_.symbol().module_name(), expression.text, fixity);
      if (expected != nullptr && kernel_domain(expected->domain)) {
        declarations.erase(
            std::remove_if(declarations.begin(), declarations.end(),
                           [&](const Module::FunctionDecl& declaration) {
                             const auto results = compiler_results(declaration);
                             return !value_inputs(declaration).empty() ||
                                    !value_results(declaration).empty() ||
                                    results.size() != 1U ||
                                    results.front().domain != expected->domain;
                           }),
            declarations.end());
      }
      if (!declarations.empty()) {
        return call(expression, range, expected, std::move(declarations));
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

  std::optional<StagedValue> unsupported(SyntaxRange range) {
    report("compiler function '" + function_.symbol().qualified_name() +
               "' contains an unsupported expression",
           range);
    return std::nullopt;
  }

  std::optional<Module::ParameterDecl>
  compiler_binding_domain(const BindingSyntax& binding) {
    if (!binding.type) {
      return std::nullopt;
    }
    if (kernel_domain(binding.type->value)) {
      return Module::ParameterDecl{binding.name, binding.type->value, false,
                                   std::nullopt};
    }
    const Module::ParameterDecl expected_type{
        "binding type", domain_expression(ValueKind::Type), false,
        std::nullopt};
    auto value = evaluate_known_expression(
        compiler_, function_.symbol().module_name(), binding.type->value,
        expected_type, locals_.known_bindings(), diagnostics_,
        SourceRange{body_.source, binding.type->range.begin,
                    binding.type->range.end},
        !under_residual_control_);
    const Type* type = value ? value->as_type() : nullptr;
    auto domain = type ? type_domain(*type) : std::nullopt;
    return domain ? std::optional<Module::ParameterDecl>{{binding.name,
                                                          std::move(*domain),
                                                          false, std::nullopt}}
                  : std::nullopt;
  }

  Flow sequence(std::span<const StatementSyntax> code) {
    for (const StatementSyntax& statement : code) {
      if (!step(statement.range)) {
        return {Control::Error, {}};
      }
      if (statement.kind == StatementSyntax::Kind::Return) {
        if (statement.values.size() != function_.results().size()) {
          report("compiler return does not match its function signature",
                 statement.range);
          return {Control::Error, {}};
        }
        std::vector<StagedValue> values;
        values.reserve(statement.values.size());
        for (std::size_t index = 0; index < statement.values.size(); ++index) {
          auto value = evaluate(statement.values[index].value,
                                statement.values[index].range,
                                &function_.results()[index]);
          if (!value) {
            return {Control::Error, {}};
          }
          values.push_back(std::move(*value));
        }
        return {Control::Return, std::move(values)};
      }
      if (statement.kind == StatementSyntax::Kind::Break) {
        return {Control::Break, {}};
      }
      if (statement.kind == StatementSyntax::Kind::Continue) {
        return {Control::Continue, {}};
      }
      if (statement.kind == StatementSyntax::Kind::If) {
        const Module::ParameterDecl condition{
            "condition", domain_expression(ValueKind::Boolean), false,
            std::nullopt};
        auto value = evaluate(statement.expression.value,
                              statement.expression.range, &condition);
        const auto selected = value ? known_boolean(*value) : std::nullopt;
        if (!selected) {
          report("compiler if condition must be bool", statement.range);
          return {Control::Error, {}};
        }
        locals_.push();
        Flow flow = sequence(*selected ? std::span(statement.body)
                                       : std::span(statement.otherwise));
        locals_.pop();
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
          const auto selected = value ? known_boolean(*value) : std::nullopt;
          if (!selected) {
            report("compiler while condition must be bool", statement.range);
            return {Control::Error, {}};
          }
          if (!*selected) {
            break;
          }
          locals_.push();
          Flow flow = sequence(statement.body);
          locals_.pop();
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
      if (statement.kind == StatementSyntax::Kind::For) {
        if (statement.iterator && statement.iterator->type) {
          report("a typed for iterator requires Residual materialization",
                 statement.iterator->range);
          return {Control::Error, {}};
        }
        auto iterable = evaluate(statement.expression.value,
                                 statement.expression.range, nullptr);
        const ExecutionValue* payload =
            iterable ? iterable->known_value() : nullptr;
        auto elements = payload ? list_elements(*payload)
                                : std::optional<std::vector<ExecutionValue>>{};
        if (!elements) {
          report("compiler for iterable must be a Known list",
                 statement.expression.range);
          return {Control::Error, {}};
        }
        for (ExecutionValue& element : *elements) {
          if (!step(statement.range)) {
            return {Control::Error, {}};
          }
          auto value = known(std::move(element), statement.expression.range);
          if (!value) {
            return {Control::Error, {}};
          }
          locals_.push();
          if (!statement.iterator ||
              !locals_.define(statement.iterator->name, std::move(*value))) {
            report("cannot define compiler for iterator", statement.range);
            locals_.pop();
            return {Control::Error, {}};
          }
          Flow flow = sequence(statement.body);
          locals_.pop();
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
      using Kind = Module::Expression::Kind;
      const Kind kind = statement.expression.value.kind;
      const bool call_expression = kind == Kind::Call || kind == Kind::Prefix ||
                                   kind == Kind::Infix || kind == Kind::Postfix;
      if (call_expression && statement.bindings.size() != 1U) {
        auto values =
            call_values(statement.expression.value, statement.expression.range,
                        statement.bindings.size());
        if (!values) {
          return {Control::Error, {}};
        }
        for (std::size_t index = 0; index < statement.bindings.size();
             ++index) {
          const BindingSyntax& binding = statement.bindings[index];
          const bool bound =
              binding.rebind
                  ? locals_.assign(binding.name, std::move((*values)[index]))
                  : locals_.define(binding.name, std::move((*values)[index]));
          if (!bound) {
            report(binding.rebind ? "cannot rebind unknown compiler value '" +
                                        binding.name + "'"
                                  : "compiler value '" + binding.name +
                                        "' is already defined in this scope",
                   binding.range);
            return {Control::Error, {}};
          }
        }
        continue;
      }
      auto expected = statement.bindings.size() == 1U
                          ? compiler_binding_domain(statement.bindings.front())
                          : std::nullopt;
      auto value =
          evaluate(statement.expression.value, statement.expression.range,
                   expected ? &*expected : nullptr);
      if (!value) {
        return {Control::Error, {}};
      }
      if (statement.bindings.empty()) {
        continue;
      }
      const BindingSyntax& binding = statement.bindings.front();
      if (binding.rebind) {
        if (!locals_.assign(binding.name, std::move(*value))) {
          report("cannot rebind unknown compiler value '" + binding.name + "'",
                 binding.range);
          return {Control::Error, {}};
        }
      } else if (!locals_.define(binding.name, std::move(*value))) {
        report("compiler value '" + binding.name +
                   "' is already defined in this scope",
               binding.range);
        return {Control::Error, {}};
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
  Locals locals_;
};

}  // namespace

std::optional<ExecutionValues> execute_call(
    Compiler& compiler, std::string_view owner,
    const Module::Expression& expression, SourceRange call_site,
    std::size_t result_count,
    std::span<const Module::ParameterDecl> expected_results,
    Diagnostics& diagnostics, const EvaluateCallArgument& evaluate,
    const ExecuteFunction& execute,
    std::span<const Module::FunctionDecl> declarations) {
  const auto report = [&](std::string message) {
    diagnostics.report(std::move(message), call_site);
  };
  std::vector<Module::FunctionDecl> visible;
  if (declarations.empty()) {
    visible = visible_functions(compiler, owner, expression.text);
    declarations = visible;
  }
  std::vector<CallCandidate> candidates;
  for (const auto& declaration : declarations) {
    auto shaped = call_candidate(declaration, expression);
    const bool expected_match =
        expected_results.empty() ||
        (expected_results.size() == declaration.results().size() &&
         std::equal(expected_results.begin(), expected_results.end(),
                    declaration.results().begin(),
                    [](const auto& expected, const auto& declared) {
                      return expected.domain == declared.domain;
                    }));
    if (shaped && declaration.results().size() == result_count &&
        expected_match) {
      candidates.push_back(std::move(*shaped));
    }
  }
  if (candidates.empty()) {
    report("no overload of '" + expression.text + "' accepts this call shape");
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
    const std::size_t before = diagnostics.size();
    auto value = evaluate(expression.arguments[index],
                          common ? &first_parameter : nullptr);
    if (!value) {
      if (diagnostics.size() == before) {
        report("compiler call argument is not Known");
      }
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
              if (!CompilerAccess::accepts(
                      compiler, candidate.function, parameter,
                      execution_value_type(supplied[index]))) {
                return true;
              }
            }
            return false;
          }),
      candidates.end());
  if (candidates.empty()) {
    report("no overload of '" + expression.text +
           "' accepts the evaluated argument types");
    return std::nullopt;
  }
  if (candidates.size() != 1U) {
    std::string message =
        "call to '" + expression.text + "' is ambiguous between";
    for (const auto& candidate : candidates) {
      message += " '" + candidate.function.symbol().qualified_name() + "'";
    }
    report(std::move(message));
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
      bound[index] =
          value ? execution_value(*value, parameters[index]) : std::nullopt;
    }
    if (!bound[index]) {
      report("compiler call is missing argument '" + parameters[index].name +
             "'");
      return std::nullopt;
    }
    arguments.push_back(std::move(*bound[index]));
  }
  auto results = execute(selected.function, std::move(arguments), call_site);
  if (!results || results->size() != result_count) {
    if (results) {
      report("compiler call returned the wrong number of values");
    }
    return std::nullopt;
  }
  return results;
}

std::optional<ExecutionValues>
execute_body(Compiler& compiler, const Module::FunctionDecl& function,
             const FunctionBody& body,
             std::span<const ExecutionValue> arguments,
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

bool verify_body_calls(Compiler& compiler, const Module::FunctionDecl& function,
                       const FunctionBody& body, Diagnostics& diagnostics) {
  const std::size_t before = diagnostics.size();
  const auto report = [&](std::string message, SyntaxRange range) {
    diagnostics.report(std::move(message),
                       SourceRange{body.source, range.begin, range.end});
  };
  const auto verify_expression = [&](const auto& self,
                                     const ExpressionSyntax& syntax) -> void {
    using Kind = Module::Expression::Kind;
    const Module::Expression& expression = syntax.value;
    if (expression.kind == Kind::Call) {
      const auto declarations = visible_functions(
          compiler, function.symbol().module_name(), expression.text);
      const bool shaped = std::any_of(
          declarations.begin(), declarations.end(), [&](const auto& current) {
            return call_candidate(current, expression).has_value();
          });
      if (!shaped) {
        report("no visible overload of '" + expression.text +
                   "' accepts this call shape",
               syntax.range);
      }
    } else if (expression.kind == Kind::Prefix ||
               expression.kind == Kind::Infix ||
               expression.kind == Kind::Postfix) {
      const auto fixity = expression.kind == Kind::Prefix
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
      if (!shaped) {
        report("no visible function defines operator '" + expression.text +
                   "' with this fixity and arity",
               syntax.range);
      }
    }
    for (const auto& argument : expression.arguments) {
      self(self, ExpressionSyntax{argument, syntax.range});
    }
  };
  const auto verify_statements =
      [&](const auto& self, std::span<const StatementSyntax> code) -> void {
    for (const StatementSyntax& statement : code) {
      if (statement.kind == StatementSyntax::Kind::Expression ||
          statement.kind == StatementSyntax::Kind::If ||
          statement.kind == StatementSyntax::Kind::While ||
          statement.kind == StatementSyntax::Kind::For) {
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
