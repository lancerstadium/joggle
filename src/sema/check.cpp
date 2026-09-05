#include "sema/check.h"

#include "sema/call.h"
#include "sema/domain.h"
#include "lang/expr.h"
#include "ir/mod.h"
#include "lang/prelude.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace joggle::detail {
namespace {

std::string resolve_prefix(const Mod& scope, std::string_view prefix) {
  if (prefix == scope.name() || prefix == prelude_mod_name) {
    return std::string(prefix);
  }
  const auto imported = std::find_if(
      scope.imports().begin(), scope.imports().end(),
      [&](const Mod::Import& value) { return value.prefix() == prefix; });
  return imported == scope.imports().end() ? std::string(prefix)
                                           : imported->name;
}

std::optional<Mod::Expr>
immediate_domain(const Mod::Expr& expression,
                 std::span<const Mod::FnDecl::GenericDecl> generics,
                 std::span<const Mod::ParamDecl> locals) {
  using Kind = Mod::Expr::Kind;
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
               ? std::optional<Mod::Expr>{}
               : std::optional<Mod::Expr>{generic->domain};
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
  return std::nullopt;
}

class ExprCheck {
public:
  ExprCheck(const Compiler& compiler, const Mod& scope,
            std::span<const Mod::FnDecl::GenericDecl> generics,
            std::span<const Mod::ParamDecl> locals, Diag& diagnostics,
            std::optional<Loc> source, std::string_view subject,
            bool public_contract)
      : compiler_(compiler), scope_(scope), generics_(generics),
        locals_(locals), diagnostics_(diagnostics), source_(std::move(source)),
        subject_(subject), public_contract_(public_contract) {}

  bool run(const Mod::Expr& expression, const Mod::Expr& expected) {
    const std::size_t before = diagnostics_.size();
    check(expression, expected);
    return diagnostics_.size() == before;
  }

private:
  void report(std::string message) {
    diagnostics_.report("in " + subject_ + ": " + std::move(message), source_);
  }

  const Mod::FnDecl::GenericDecl* generic(std::string_view name) const {
    const auto found = std::find_if(
        generics_.begin(), generics_.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    return found == generics_.end() ? nullptr : &*found;
  }

  const Mod::ParamDecl* local(std::string_view name) const {
    const auto found = std::find_if(
        locals_.begin(), locals_.end(),
        [&](const auto& candidate) { return candidate.name == name; });
    return found == locals_.end() ? nullptr : &*found;
  }

  bool check_derived_field(const Mod::Expr& expression,
                           const Mod::Expr& expected) {
    if (expression.kind != Mod::Expr::Kind::Reference ||
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

  void check(const Mod::Expr& expression, const Mod::Expr& expected) {
    using Kind = Mod::Expr::Kind;
    const auto domain = compiler_domain(expected);
    if (!domain) {
      report("unknown compiler domain");
      return;
    }
    if (expression.kind == Kind::FnType) {
      const auto signature = callable_type(expression);
      if (domain->list || domain->element != ValKind::Type || !signature) {
        report("malformed fn type");
        return;
      }
      const auto type_domain = domain_expression(ValKind::Type);
      for (const auto side : {signature->inputs, signature->results}) {
        for (const auto& element : side) {
          check(element, type_domain);
        }
      }
      return;
    }
    if (expression.kind == Kind::Lambda) {
      const bool inferred =
          expression.arguments.size() == expression.labels.size() + 1U;
      const bool annotated =
          expression.arguments.size() == expression.labels.size() + 2U;
      if (domain->list || domain->element != ValKind::Fn ||
          (!inferred && !annotated)) {
        report("lambda has the wrong compiler domain");
        return;
      }
      const auto type_domain = domain_expression(ValKind::Type);
      for (std::size_t index = 0; index < expression.labels.size(); ++index) {
        if (expression.arguments[index].kind != Kind::Infer) {
          check(expression.arguments[index], type_domain);
        }
      }
      if (annotated) {
        check(expression.arguments[expression.labels.size()], type_domain);
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
      check(expression.arguments[0], domain_expression(ValKind::Boolean));
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
                            (domain->element == ValKind::Integer ||
                             domain->element == ValKind::Real)) ||
                           (expression.kind == Kind::Boolean &&
                            domain->element == ValKind::Boolean) ||
                           (expression.kind == Kind::String &&
                            domain->element == ValKind::String);
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

  void check_operator(const Mod::Expr& expression, const Mod::Expr& expected) {
    using Kind = Mod::Expr::Kind;
    const std::size_t arity =
        expression.kind == Kind::Infix ? expression.arguments.size() : 1U;
    if (expression.arguments.size() != arity) {
      report("malformed operator expression");
      return;
    }
    const auto fixity =
        expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
        : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                           : Mod::FnDecl::Fixity::Infix;
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

  void check_call(const Mod::Expr& expression, const Mod::Expr& expected) {
    std::vector<CallCandidate> candidates;
    for (const auto& fn :
         visible_fns(compiler_, scope_.name(), expression.text)) {
      auto candidate = call_candidate(fn, expression);
      const auto results = compiler_results(fn);
      if (!candidate || !value_inputs(fn).empty() ||
          !value_results(fn).empty() || results.size() != 1U ||
          results.front().domain != expected) {
        continue;
      }
      bool accepts = true;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        const auto actual =
            immediate_domain(expression.arguments[index], generics_, locals_);
        if (actual &&
            fn.inputs()[candidate->parameters[index]].domain != *actual) {
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
                       "' has no matching compiler fn"
                 : "call to '" + expression.text + "' is ambiguous");
      return;
    }
    const auto& selected = candidates.front();
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      check(expression.arguments[index],
            selected.fn.inputs()[selected.parameters[index]].domain);
    }
  }

  void check_declaration_reference(const Mod::Expr& expression, Domain domain) {
    if (!domain.list && domain.element == ValKind::Mod) {
      if (expression.arguments.empty() &&
          visible_mod(compiler_, scope_.name(), expression.text)) {
        return;
      }
      report("unknown Mod package '" + expression.text + "'");
      return;
    }
    if (domain.element != ValKind::Type) {
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
    const auto mod = compiler_.mod(owner);
    if (!mod) {
      report("reference uses missing Mod '" + owner + "'");
      return;
    }
    const auto target = mod->type(name);
    if (!target) {
      report("unknown type '" + expression.text + "'");
      return;
    }
    if ((owner != scope_.name() || public_contract_) && !target->exported()) {
      report("type '" + expression.text + "' is private");
      return;
    }
    const std::span<const Mod::ParamDecl> parameters = target->parameters();
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
  const Mod& scope_;
  std::span<const Mod::FnDecl::GenericDecl> generics_;
  std::span<const Mod::ParamDecl> locals_;
  Diag& diagnostics_;
  std::optional<Loc> source_;
  std::string subject_;
  bool public_contract_ = false;
};

}  // namespace

bool check_declaration_expression(
    const Compiler& compiler, const Mod& scope, const Mod::Expr& expression,
    const Mod::Expr& expected,
    std::span<const Mod::FnDecl::GenericDecl> generics,
    std::span<const Mod::ParamDecl> locals, Diag& diagnostics,
    std::optional<Loc> source, std::string_view subject, bool public_contract) {
  return ExprCheck(compiler, scope, generics, locals, diagnostics,
                   std::move(source), subject, public_contract)
      .run(expression, expected);
}

}  // namespace joggle::detail
