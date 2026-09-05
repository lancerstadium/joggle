#include "compile/eval.h"

#include "sema/call.h"
#include "compile/compiler.h"
#include "lang/fn.h"
#include "ir/mod.h"
#include "ir/type.h"
#include "sema/infer.h"

#include <algorithm>
#include <charconv>
#include <locale>
#include <sstream>
#include <typeinfo>
#include <unordered_set>
#include <utility>

namespace joggle::detail {
namespace {

class BodyEvaluator {
  struct Flow {
    Control control = Control::Next;
    std::vector<StagedVal> values;
  };

public:
  BodyEvaluator(Compiler& compiler, const Mod::FnDecl& fn, const FnBody& body,
                std::span<const ExecVal> arguments, Compiler::Limits limits,
                std::size_t& steps, bool under_residual_control,
                Diag& diagnostics, const ExecuteFn& execute)
      : compiler_(compiler), fn_(fn), body_(body), limits_(limits),
        steps_(steps), under_residual_control_(under_residual_control),
        diagnostics_(diagnostics), execute_(execute) {
    locals_.push();
    const auto& contract = FnTypeAccess::get(fn_);
    for (std::size_t index = 0; index < fn_.inputs().size(); ++index) {
      auto value = stage(compiler_, arguments[index]);
      if (!value) {
        report("compiler fn argument has no resolved Joggle type", body_.range);
        continue;
      }
      const auto& parameter = fn_.inputs()[index];
      locals_.define(parameter.name, *value);
      if (index < contract.bindings.size() && contract.bindings[index] &&
          contract.bindings[index]->kind == Mod::Expr::Kind::Variable) {
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
    valid_ = bind_generics(arguments);
  }

  std::optional<ExecVals> run() {
    if (!valid_) {
      return std::nullopt;
    }
    Flow flow = sequence(body_.blocks.front().statements);
    if (flow.control != Control::Return ||
        flow.values.size() != fn_.results().size()) {
      if (flow.control != Control::Error) {
        report("compiler fn path falls through without returning", body_.range);
      }
      return std::nullopt;
    }
    ExecVals results;
    results.reserve(flow.values.size());
    for (const StagedVal& staged : flow.values) {
      const ExecVal* value = staged.known_value();
      if (value == nullptr) {
        report("compiler fn returned a Residual value", body_.range);
        return std::nullopt;
      }
      results.push_back(*value);
    }
    return results;
  }

private:
  bool bind_generics(std::span<const ExecVal> arguments) {
    std::vector<Type> value_arguments;
    std::vector<std::optional<ParamVal>> known_arguments;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (is_value_port(fn_.inputs()[index])) {
        auto type = execution_type(compiler_, arguments[index]);
        if (!type) {
          return false;
        }
        value_arguments.push_back(std::move(*type));
      } else {
        known_arguments.push_back(parameter_value(arguments[index]));
      }
    }
    std::vector<std::optional<Type>> expected_results(
        value_results(fn_).size());
    auto resolved =
        resolve_call_types(compiler_, fn_, value_arguments, known_arguments,
                           expected_results, diagnostics_);
    if (!resolved) {
      return false;
    }
    for (const auto& [name, binding] : resolved->bindings) {
      const Mod::ParamDecl parameter{name, binding.domain.value_or(Mod::Expr{}),
                                     false, std::nullopt};
      auto raw = exec_val(binding.value, parameter);
      auto value =
          raw ? stage(compiler_, std::move(*raw)) : std::optional<StagedVal>{};
      if (!value) {
        report("generic '" + name + "' has no executable value", body_.range);
        return false;
      }
      if (const auto* existing = local(name)) {
        if (!same_staged_value(*existing, *value)) {
          report("generic '" + name + "' is bound to different compiler values",
                 body_.range);
          return false;
        }
      } else if (!locals_.define(name, std::move(*value))) {
        return false;
      }
    }
    return true;
  }

  void report(std::string message, SyntaxRange range) {
    diagnostics_.report(std::move(message),
                        Loc{body_.source, range.begin, range.end});
  }

  bool step(SyntaxRange range) {
    if (steps_++ < limits_.steps) {
      return true;
    }
    report("compiler execution step limit exceeded", range);
    return false;
  }

  StagedVal* local(std::string_view name) { return locals_.find(name); }

  std::optional<StagedVal> known(ExecVal value, SyntaxRange range) {
    auto result = stage(compiler_, std::move(value));
    if (!result) {
      report("compiler value has no resolved Joggle type", range);
    }
    return result;
  }

  std::optional<StagedVal> list(const Mod::Expr& expression, SyntaxRange range,
                                const Mod::ParamDecl* expected) {
    std::vector<ExecVal> elements;
    elements.reserve(expression.arguments.size());
    for (const auto& element : expression.arguments) {
      auto value = evaluate(element, range, nullptr);
      if (!value) {
        return std::nullopt;
      }
      const ExecVal* known = value->known_value();
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
        return exec_val_type(elements.front());
      }
      if (!domain || !domain->list) {
        return {};
      }
      switch (domain->element) {
      case ValKind::Integer:
        return typeid(std::int64_t).name();
      case ValKind::Real:
        return typeid(double).name();
      case ValKind::Boolean:
        return typeid(bool).name();
      case ValKind::String:
        return typeid(std::string).name();
      case ValKind::Type:
        return typeid(Type).name();
      case ValKind::Bytes:
      case ValKind::Fn:
      case ValKind::Mod:
        return {};
      }
      return {};
    }();
    if (element_type.empty() || !std::all_of(elements.begin(), elements.end(),
                                             [&](const ExecVal& element) {
                                               return exec_val_type(element) ==
                                                      element_type;
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
      return known(ExecVal{std::move(result)}, range);
    }
    if (element_type == typeid(double).name()) {
      RealList result;
      for (auto& element : elements) {
        result.push_back(std::get<double>(element));
      }
      return known(ExecVal{std::move(result)}, range);
    }
    if (element_type == typeid(bool).name()) {
      BooleanList result;
      for (auto& element : elements) {
        result.push_back(std::get<bool>(element));
      }
      return known(ExecVal{std::move(result)}, range);
    }
    if (element_type == typeid(std::string).name()) {
      StringList result;
      for (auto& element : elements) {
        result.push_back(std::get<std::string>(std::move(element)));
      }
      return known(ExecVal{std::move(result)}, range);
    }
    if (element_type == typeid(Type).name()) {
      TypeList result;
      for (auto& element : elements) {
        result.push_back(std::get<Type>(std::move(element)));
      }
      return known(ExecVal{std::move(result)}, range);
    }
    report("compiler list element type is not representable", range);
    return std::nullopt;
  }

  std::optional<StagedVal> known_expression(const Mod::Expr& expression,
                                            SyntaxRange range,
                                            const Mod::ParamDecl& expected) {
    auto value = evaluate_known_expression(
        compiler_, fn_.symbol().mod_name(), expression, expected,
        locals_.known_bindings(), diagnostics_,
        Loc{body_.source, range.begin, range.end}, !under_residual_control_);
    auto result = value ? exec_val(*value, expected) : std::optional<ExecVal>{};
    return result ? known(std::move(*result), range)
                  : std::optional<StagedVal>{};
  }

  std::optional<Mod::ParamDecl>
  infer_operator_result(const Mod::Expr& expression) {
    if (expression.arguments.empty()) {
      return std::nullopt;
    }
    const Mod::Expr& operand = expression.arguments.front();
    std::optional<Domain> domain;
    if ((operand.kind == Mod::Expr::Kind::Variable ||
         operand.kind == Mod::Expr::Kind::Reference) &&
        operand.arguments.empty()) {
      if (const auto* value = local(operand.text)) {
        const ExecVal* known = value->known_value();
        if (known != nullptr) {
          domain = cpp_value_domain(exec_val_type(*known));
        }
      }
    } else if (operand.kind == Mod::Expr::Kind::Number) {
      domain = Domain{operand.text.find_first_of(".eE") == std::string::npos
                          ? ValKind::Integer
                          : ValKind::Real,
                      false};
    }
    return domain && !domain->list
               ? std::optional<Mod::ParamDecl>{{"operator result",
                                                domain_expression(
                                                    domain->element),
                                                false, std::nullopt}}
               : std::nullopt;
  }

  std::optional<std::vector<StagedVal>>
  call_values(const Mod::Expr& expression, SyntaxRange range,
              std::size_t result_count,
              std::span<const Mod::ParamDecl> expected_results = {},
              std::vector<Mod::FnDecl> declarations = {}) {
    const Loc call_site{body_.source, range.begin, range.end};
    auto results = execute_call(
        compiler_, fn_.symbol().mod_name(), expression, call_site, result_count,
        expected_results, diagnostics_,
        [&](const Mod::Expr& argument,
            const Mod::ParamDecl* expected) -> std::optional<ExecVal> {
          auto value = evaluate(argument, range, expected);
          const ExecVal* known = value ? value->known_value() : nullptr;
          if (known == nullptr && value) {
            report("compiler call argument is Residual", range);
          }
          return known ? std::optional<ExecVal>{*known} : std::nullopt;
        },
        execute_, declarations);
    if (!results || results->size() != result_count) {
      return std::nullopt;
    }
    std::vector<StagedVal> staged;
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

  std::optional<StagedVal> call(const Mod::Expr& expression, SyntaxRange range,
                                const Mod::ParamDecl* expected,
                                std::vector<Mod::FnDecl> declarations = {}) {
    const std::span<const Mod::ParamDecl> expected_results =
        expected == nullptr ? std::span<const Mod::ParamDecl>{}
                            : std::span<const Mod::ParamDecl>{expected, 1U};
    auto values = call_values(expression, range, 1U, expected_results,
                              std::move(declarations));
    return values ? std::optional<StagedVal>{std::move(values->front())}
                  : std::nullopt;
  }

  std::optional<StagedVal> evaluate(const Mod::Expr& expression,
                                    SyntaxRange range,
                                    const Mod::ParamDecl* expected) {
    if (!step(range)) {
      return std::nullopt;
    }
    using Kind = Mod::Expr::Kind;
    if ((expression.kind == Kind::Variable ||
         expression.kind == Kind::Reference) &&
        expression.arguments.empty()) {
      if (auto* value = local(expression.text)) {
        return *value;
      }
      if (const auto mod = visible_mod(compiler_, fn_.symbol().mod_name(),
                                       expression.text)) {
        return known(store_exec_val(*mod), range);
      }
      if (expression.kind == Kind::Variable) {
        report("compiler fn '" + fn_.symbol().qualified_name() +
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
          return known(ExecVal{integer}, range);
        }
      } else {
        double real = 0.0;
        std::istringstream input(expression.text);
        input.imbue(std::locale::classic());
        input >> real;
        if (input && input.peek() == std::char_traits<char>::eof()) {
          return known(ExecVal{real}, range);
        }
      }
      report("invalid compiler numeric literal", range);
      return std::nullopt;
    }
    if (expression.kind == Kind::Boolean) {
      return known(ExecVal{expression.text == "true"}, range);
    }
    if (expression.kind == Kind::String) {
      return known(ExecVal{expression.text}, range);
    }
    if (expression.kind == Kind::Lambda) {
      const auto domain = expected == nullptr ? std::optional<Domain>{}
                                              : kernel_domain(expected->domain);
      if (!domain || domain->list || domain->element != ValKind::Fn) {
        report("compiler lambda needs a fn context", range);
        return std::nullopt;
      }
      auto fn = instantiate_lambda(
          compiler_, fn_.symbol().mod_name(), expression,
          Loc{body_.source, range.begin, range.end}, diagnostics_,
          locals_.known_bindings(), std::nullopt, std::nullopt,
          !under_residual_control_);
      return fn ? known(store_exec_val(std::move(*fn)), range) : std::nullopt;
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
      const Mod::ParamDecl condition{"condition",
                                     domain_expression(ValKind::Boolean), false,
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
      const auto fixity =
          expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                             : Mod::FnDecl::Fixity::Infix;
      auto inferred = expected == nullptr ? infer_operator_result(expression)
                                          : std::nullopt;
      if (expected == nullptr && inferred) {
        expected = &*inferred;
      }
      auto declarations = visible_operators(compiler_, fn_.symbol().mod_name(),
                                            expression.text, fixity);
      if (expected != nullptr && kernel_domain(expected->domain)) {
        declarations.erase(
            std::remove_if(declarations.begin(), declarations.end(),
                           [&](const Mod::FnDecl& declaration) {
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
    if (expression.kind == Kind::FnType ||
        (expression.kind == Kind::Reference && expected != nullptr)) {
      return known_expression(expression, range, *expected);
    }
    return expression.kind == Kind::Call ? call(expression, range, expected)
                                         : unsupported(range);
  }

  std::optional<StagedVal> unsupported(SyntaxRange range) {
    report("compiler fn '" + fn_.symbol().qualified_name() +
               "' contains an unsupported expression",
           range);
    return std::nullopt;
  }

  std::optional<Mod::ParamDecl>
  compiler_binding_domain(const BindingSyntax& binding) {
    if (!binding.type) {
      return std::nullopt;
    }
    if (kernel_domain(binding.type->value)) {
      return Mod::ParamDecl{binding.name, binding.type->value, false,
                            std::nullopt};
    }
    const Mod::ParamDecl expected_type{
        "binding type", domain_expression(ValKind::Type), false, std::nullopt};
    auto value = evaluate_known_expression(
        compiler_, fn_.symbol().mod_name(), binding.type->value, expected_type,
        locals_.known_bindings(), diagnostics_,
        Loc{body_.source, binding.type->range.begin, binding.type->range.end},
        !under_residual_control_);
    const Type* type = value ? value->as_type() : nullptr;
    auto domain = type ? type_domain(*type) : std::nullopt;
    return domain ? std::optional<Mod::ParamDecl>{{binding.name,
                                                   std::move(*domain), false,
                                                   std::nullopt}}
                  : std::nullopt;
  }

  Flow sequence(std::span<const StatementSyntax> code) {
    for (const StatementSyntax& statement : code) {
      if (!step(statement.range)) {
        return {Control::Error, {}};
      }
      if (statement.kind == StatementSyntax::Kind::Return) {
        if (statement.values.size() != fn_.results().size()) {
          report("compiler return does not match its fn signature",
                 statement.range);
          return {Control::Error, {}};
        }
        std::vector<StagedVal> values;
        values.reserve(statement.values.size());
        for (std::size_t index = 0; index < statement.values.size(); ++index) {
          auto value =
              evaluate(statement.values[index].value,
                       statement.values[index].range, &fn_.results()[index]);
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
        const Mod::ParamDecl condition{"condition",
                                       domain_expression(ValKind::Boolean),
                                       false, std::nullopt};
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
          const Mod::ParamDecl condition{"condition",
                                         domain_expression(ValKind::Boolean),
                                         false, std::nullopt};
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
        const ExecVal* payload = iterable ? iterable->known_value() : nullptr;
        auto elements = payload ? list_elements(*payload)
                                : std::optional<std::vector<ExecVal>>{};
        if (!elements) {
          report("compiler for iterable must be a Known list",
                 statement.expression.range);
          return {Control::Error, {}};
        }
        for (ExecVal& element : *elements) {
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
      using Kind = Mod::Expr::Kind;
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
  Mod::FnDecl fn_;
  const FnBody& body_;
  Compiler::Limits limits_;
  std::size_t& steps_;
  bool under_residual_control_ = false;
  Diag& diagnostics_;
  const ExecuteFn& execute_;
  Locals locals_;
  bool valid_ = false;
};

}  // namespace

std::optional<ExecVals> execute_call(
    Compiler& compiler, std::string_view owner, const Mod::Expr& expression,
    Loc call_site, std::size_t result_count,
    std::span<const Mod::ParamDecl> expected_results, Diag& diagnostics,
    const EvaluateCallArgument& evaluate, const ExecuteFn& execute,
    std::span<const Mod::FnDecl> declarations) {
  const auto report = [&](std::string message) {
    diagnostics.report(std::move(message), call_site);
  };
  std::vector<Mod::FnDecl> visible;
  if (declarations.empty()) {
    visible = visible_fns(compiler, owner, expression.text);
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

  std::vector<ExecVal> supplied;
  supplied.reserve(expression.arguments.size());
  for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
    const auto& first = candidates.front();
    const auto& first_parameter = first.fn.inputs()[first.parameters[index]];
    const bool common = std::all_of(
        candidates.begin() + 1, candidates.end(),
        [&](const CallCandidate& current) {
          return current.fn.inputs()[current.parameters[index]].domain ==
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
                  candidate.fn.inputs()[candidate.parameters[index]];
              if (!CompilerAccess::accepts(compiler, candidate.fn, parameter,
                                           exec_val_type(supplied[index]))) {
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
      message += " '" + candidate.fn.symbol().qualified_name() + "'";
    }
    report(std::move(message));
    return std::nullopt;
  }

  const CallCandidate& selected = candidates.front();
  const auto parameters = selected.fn.inputs();
  std::vector<std::optional<ExecVal>> bound(parameters.size());
  for (std::size_t index = 0; index < supplied.size(); ++index) {
    bound[selected.parameters[index]] = std::move(supplied[index]);
  }
  std::vector<ExecVal> arguments;
  arguments.reserve(parameters.size());
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!bound[index] && parameters[index].default_value) {
      const auto value = parameter_default(parameters[index]);
      bound[index] = value ? exec_val(*value, parameters[index]) : std::nullopt;
    }
    if (!bound[index]) {
      report("compiler call is missing argument '" + parameters[index].name +
             "'");
      return std::nullopt;
    }
    arguments.push_back(std::move(*bound[index]));
  }
  auto results = execute(selected.fn, std::move(arguments), call_site);
  if (!results || results->size() != result_count) {
    if (results) {
      report("compiler call returned the wrong number of values");
    }
    return std::nullopt;
  }
  return results;
}

std::optional<ExecVals>
execute_body(Compiler& compiler, const Mod::FnDecl& fn, const FnBody& body,
             std::span<const ExecVal> arguments, Compiler::Limits limits,
             std::size_t& steps, bool under_residual_control, Diag& diagnostics,
             const ExecuteFn& execute) {
  if (body.blocks.size() != 1U || body.blocks.front().terminator) {
    diagnostics.report(
        "compiler execution of fn '" + fn.symbol().qualified_name() +
        "' requires a structured body rather than explicit CFG blocks");
    return std::nullopt;
  }
  return BodyEvaluator(compiler, fn, body, arguments, limits, steps,
                       under_residual_control, diagnostics, execute)
      .run();
}

bool verify_body_calls(Compiler& compiler, const Mod::FnDecl& fn,
                       const FnBody& body, Diag& diagnostics) {
  const std::size_t before = diagnostics.size();
  std::unordered_set<std::string> locals;
  for (const Mod::ParamDecl& input : fn.inputs()) {
    locals.insert(input.name);
  }
  const auto collect = [&](const auto& self,
                           std::span<const StatementSyntax> code) -> void {
    for (const StatementSyntax& statement : code) {
      for (const BindingSyntax& binding : statement.bindings) {
        locals.insert(binding.name);
      }
      if (statement.iterator) {
        locals.insert(statement.iterator->name);
      }
      self(self, statement.body);
      self(self, statement.otherwise);
    }
  };
  for (const BlkSyntax& block : body.blocks) {
    for (const BlkArgSyntax& argument : block.arguments) {
      locals.insert(argument.name);
    }
    collect(collect, block.statements);
  }
  const auto report = [&](std::string message, SyntaxRange range) {
    diagnostics.report(std::move(message),
                       Loc{body.source, range.begin, range.end});
  };
  const auto verify_expression =
      [&](const auto& self, const ExprSyntax& syntax,
          const std::unordered_set<std::string>& visible_locals) -> void {
    using Kind = Mod::Expr::Kind;
    const Mod::Expr& expression = syntax.value;
    if (expression.kind == Kind::Lambda) {
      auto nested_locals = visible_locals;
      nested_locals.insert(expression.labels.begin(), expression.labels.end());
      for (const auto& argument : expression.arguments) {
        self(self, ExprSyntax{argument, syntax.range}, nested_locals);
      }
      return;
    }
    if (expression.kind == Kind::Call &&
        !visible_locals.contains(expression.text)) {
      const auto declarations =
          visible_fns(compiler, fn.symbol().mod_name(), expression.text);
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
      const auto fixity =
          expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                             : Mod::FnDecl::Fixity::Infix;
      const auto declarations = visible_operators(
          compiler, fn.symbol().mod_name(), expression.text, fixity);
      const bool shaped = std::any_of(
          declarations.begin(), declarations.end(), [&](const auto& current) {
            return call_candidate(current, expression).has_value();
          });
      if (!shaped) {
        report("no visible fn defines operator '" + expression.text +
                   "' with this fixity and arity",
               syntax.range);
      }
    }
    for (const auto& argument : expression.arguments) {
      self(self, ExprSyntax{argument, syntax.range}, visible_locals);
    }
  };
  const auto verify_statements =
      [&](const auto& self, std::span<const StatementSyntax> code) -> void {
    for (const StatementSyntax& statement : code) {
      if (statement.kind == StatementSyntax::Kind::Expr ||
          statement.kind == StatementSyntax::Kind::If ||
          statement.kind == StatementSyntax::Kind::While ||
          statement.kind == StatementSyntax::Kind::For) {
        verify_expression(verify_expression, statement.expression, locals);
      }
      for (const auto& value : statement.values) {
        verify_expression(verify_expression, value, locals);
      }
      self(self, statement.body);
      self(self, statement.otherwise);
    }
  };
  for (const BlkSyntax& block : body.blocks) {
    verify_statements(verify_statements, block.statements);
    if (!block.terminator) {
      continue;
    }
    if (block.terminator->condition) {
      verify_expression(verify_expression, *block.terminator->condition,
                        locals);
    }
    for (const auto& value : block.terminator->values) {
      verify_expression(verify_expression, value, locals);
    }
    for (const auto& successor : block.terminator->successors) {
      for (const auto& argument : successor.arguments) {
        verify_expression(verify_expression, argument, locals);
      }
    }
  }
  return diagnostics.size() == before;
}

}  // namespace joggle::detail
