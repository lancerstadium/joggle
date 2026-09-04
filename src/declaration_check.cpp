#include "declaration_check.h"

#include "call_resolution.h"
#include "domain.h"
#include "expression_syntax.h"
#include "module_internal.h"
#include "prelude.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace joggle::detail {
namespace {

std::string resolve_prefix(const Module& scope, std::string_view prefix) {
  if (prefix == scope.name() || prefix == prelude_module_name) {
    return std::string(prefix);
  }
  const auto imported = std::find_if(
      scope.imports().begin(), scope.imports().end(),
      [&](const Module::Import& value) { return value.prefix() == prefix; });
  return imported == scope.imports().end() ? std::string(prefix)
                                           : imported->name;
}

std::optional<Module::Expression>
immediate_domain(const Module::Expression& expression,
                 std::span<const Module::FunctionDecl::GenericDecl> generics,
                 std::span<const Module::ParameterDecl> locals) {
  using Kind = Module::Expression::Kind;
  if (expression.kind == Kind::Variable) {
    const auto local =
        std::find_if(locals.begin(), locals.end(), [&](const auto& candidate) {
          return candidate.name == expression.text;
        });
    if (local != locals.end()) {
      return local->domain;
    }
    const auto generic = std::find_if(
        generics.begin(), generics.end(), [&](const auto& candidate) {
          return candidate.name == expression.text;
        });
    return generic == generics.end()
               ? std::optional<Module::Expression>{}
               : std::optional<Module::Expression>{generic->domain};
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

class ExpressionCheck {
public:
  ExpressionCheck(const Compiler& compiler, const Module& scope,
                  std::span<const Module::FunctionDecl::GenericDecl> generics,
                  std::span<const Module::ParameterDecl> locals,
                  Diagnostics& diagnostics, std::optional<SourceRange> source,
                  std::string_view subject)
      : compiler_(compiler), scope_(scope), generics_(generics),
        locals_(locals), diagnostics_(diagnostics), source_(std::move(source)),
        subject_(subject) {}

  bool run(const Module::Expression& expression,
           const Module::Expression& expected) {
    const std::size_t before = diagnostics_.size();
    check(expression, expected);
    return diagnostics_.size() == before;
  }

private:
  void report(std::string message) {
    diagnostics_.report("in " + subject_ + ": " + std::move(message), source_);
  }

  const Module::FunctionDecl::GenericDecl*
  generic(std::string_view name) const {
    const auto found = std::find_if(
        generics_.begin(), generics_.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    return found == generics_.end() ? nullptr : &*found;
  }

  const Module::ParameterDecl* local(std::string_view name) const {
    const auto found = std::find_if(
        locals_.begin(), locals_.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    return found == locals_.end() ? nullptr : &*found;
  }

  bool check_derived_field(const Module::Expression& expression,
                           const Module::Expression& expected) {
    if (expression.kind != Module::Expression::Kind::Reference ||
        !expression.arguments.empty()) {
      return false;
    }
    const std::size_t dot = expression.text.find('.');
    if (dot == std::string::npos) {
      return false;
    }
    const std::string_view receiver(expression.text.data(), dot);
    const auto* parameter = generic(receiver);
    if (parameter == nullptr) {
      return false;
    }
    // A generic type's computed fields are checked when the generic is bound
    // to a concrete TypeDecl.
    static_cast<void>(expected);
    return true;
  }

  void check(const Module::Expression& expression,
             const Module::Expression& expected) {
    using Kind = Module::Expression::Kind;
    const auto domain = kernel_domain(expected);
    if (!domain) {
      report("unknown compiler domain");
      return;
    }
    if (expression.kind == Kind::FunctionType) {
      const auto signature = callable_type(expression);
      if (domain->list || domain->element != ValueKind::Type || !signature) {
        report("malformed function type");
        return;
      }
      const auto type_domain = domain_expression(ValueKind::Type);
      for (const auto side : {signature->inputs, signature->results}) {
        for (const auto& element : side) {
          check(element, type_domain);
        }
      }
      return;
    }
    if (expression.kind == Kind::Lambda) {
      if (domain->list || domain->element != ValueKind::Function ||
          expression.arguments.empty() ||
          expression.labels.size() + 1U != expression.arguments.size()) {
        report("lambda has the wrong compiler domain");
        return;
      }
      const auto type_domain = domain_expression(ValueKind::Type);
      for (std::size_t index = 0; index < expression.labels.size(); ++index) {
        check(expression.arguments[index], type_domain);
      }
      return;
    }
    if (expression.kind == Kind::Variable) {
      const auto* variable = local(expression.text);
      const auto* parameter = generic(expression.text);
      const auto* actual = variable != nullptr    ? &variable->domain
                           : parameter != nullptr ? &parameter->domain
                                                  : nullptr;
      if (actual == nullptr || *actual != expected) {
        report("variable '" + expression.text + "' has the wrong domain");
      }
      return;
    }
    if (expression.kind == Kind::Evaluate) {
      if (expression.arguments.size() != 1U) {
        report("malformed compile-time evaluation");
        return;
      }
      check(expression.arguments.front(), expected);
      return;
    }
    if (expression.kind == Kind::If) {
      if (expression.arguments.size() != 3U) {
        report("malformed if expression");
        return;
      }
      check(expression.arguments[0], domain_expression(ValueKind::Boolean));
      check(expression.arguments[1], expected);
      check(expression.arguments[2], expected);
      return;
    }
    if (domain->list && expression.kind != Kind::Call &&
        expression.kind != Kind::Reference) {
      if (expression.kind != Kind::List) {
        report("expected a list expression");
        return;
      }
      for (const auto& element : expression.arguments) {
        check(element, domain_expression(domain->element));
      }
      return;
    }
    if (expression.kind == Kind::List) {
      report("unexpected list expression");
      return;
    }
    if (expression.kind == Kind::Prefix || expression.kind == Kind::Infix ||
        expression.kind == Kind::Postfix) {
      check_operator(expression, expected);
      return;
    }
    if (expression.kind == Kind::Call) {
      check_call(expression, expected);
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
        report("literal has the wrong domain");
      }
      return;
    }
    if (check_derived_field(expression, expected)) {
      return;
    }
    check_declaration_reference(expression, *domain);
  }

  void check_operator(const Module::Expression& expression,
                      const Module::Expression& expected) {
    using Kind = Module::Expression::Kind;
    const std::size_t arity = expression.kind == Kind::Infix ? 2U : 1U;
    if (expression.arguments.size() != arity) {
      report("malformed operator expression");
      return;
    }
    const auto fixity = expression.kind == Kind::Prefix
                            ? Module::FunctionDecl::Fixity::Prefix
                        : expression.kind == Kind::Postfix
                            ? Module::FunctionDecl::Fixity::Postfix
                            : Module::FunctionDecl::Fixity::Infix;
    auto candidates = operator_candidates(
        compiler_, scope_.name(), expression.text, fixity, arity, expected);
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [&](const auto& candidate) {
                         for (std::size_t index = 0; index < arity; ++index) {
                           const auto actual = immediate_domain(
                               expression.arguments[index], generics_, locals_);
                           if (actual &&
                               candidate.inputs()[index].domain != *actual) {
                             return true;
                           }
                         }
                         return false;
                       }),
        candidates.end());
    if (candidates.size() != 1U) {
      report(candidates.empty()
                 ? "operator '" + expression.text +
                       "' has no matching declaration"
                 : "operator '" + expression.text + "' is ambiguous");
      return;
    }
    for (std::size_t index = 0; index < arity; ++index) {
      check(expression.arguments[index],
            candidates.front().inputs()[index].domain);
    }
  }

  void check_call(const Module::Expression& expression,
                  const Module::Expression& expected) {
    std::vector<CallCandidate> candidates;
    for (const auto& function :
         visible_functions(compiler_, scope_.name(), expression.text)) {
      auto candidate = call_candidate(function, expression);
      const auto results = compiler_results(function);
      if (!candidate || !value_inputs(function).empty() ||
          !value_results(function).empty() || results.size() != 1U ||
          results.front().domain != expected) {
        continue;
      }
      bool accepts = true;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        const auto actual =
            immediate_domain(expression.arguments[index], generics_, locals_);
        if (actual &&
            function.inputs()[candidate->parameters[index]].domain != *actual) {
          accepts = false;
          break;
        }
      }
      if (accepts) {
        candidates.push_back(std::move(*candidate));
      }
    }
    if (candidates.size() != 1U) {
      report(candidates.empty()
                 ? "call to '" + expression.text +
                       "' has no matching compiler function"
                 : "call to '" + expression.text + "' is ambiguous");
      return;
    }
    const auto& selected = candidates.front();
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      check(expression.arguments[index],
            selected.function.inputs()[selected.parameters[index]].domain);
    }
  }

  void check_declaration_reference(const Module::Expression& expression,
                                   Domain domain) {
    if (domain.element != ValueKind::Type) {
      report("reference '" + expression.text + "' has the wrong domain");
      return;
    }
    const std::size_t dot = expression.text.find('.');
    const std::string owner =
        dot == std::string::npos
            ? std::string(scope_.name())
            : resolve_prefix(scope_,
                             std::string_view(expression.text).substr(0, dot));
    const std::string_view name =
        dot == std::string::npos
            ? std::string_view(expression.text)
            : std::string_view(expression.text).substr(dot + 1U);
    const auto module = compiler_.module(owner);
    if (!module) {
      report("reference uses missing Module '" + owner + "'");
      return;
    }
    const auto target = module->type(name);
    if (!target) {
      report("unknown type '" + expression.text + "'");
      return;
    }
    const std::span<const Module::ParameterDecl> parameters =
        target->parameters();
    if (expression.arguments.size() > parameters.size()) {
      report("'" + expression.text + "' has too many arguments");
      return;
    }
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      check(expression.arguments[index], parameters[index].domain);
    }
    for (std::size_t index = expression.arguments.size();
         index < parameters.size(); ++index) {
      if (!parameters[index].default_value) {
        report("'" + expression.text + "' omits argument '" +
               parameters[index].name + "'");
      }
    }
  }

  const Compiler& compiler_;
  const Module& scope_;
  std::span<const Module::FunctionDecl::GenericDecl> generics_;
  std::span<const Module::ParameterDecl> locals_;
  Diagnostics& diagnostics_;
  std::optional<SourceRange> source_;
  std::string subject_;
};

}  // namespace

bool check_declaration_expression(
    const Compiler& compiler, const Module& scope,
    const Module::Expression& expression, const Module::Expression& expected,
    std::span<const Module::FunctionDecl::GenericDecl> generics,
    std::span<const Module::ParameterDecl> locals, Diagnostics& diagnostics,
    std::optional<SourceRange> source, std::string_view subject) {
  return ExpressionCheck(compiler, scope, generics, locals, diagnostics,
                         std::move(source), subject)
      .run(expression, expected);
}

}  // namespace joggle::detail
