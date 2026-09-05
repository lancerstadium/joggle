#include "joggle/transform.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "joggle/compiler.h"
#include "ir/fn.h"
#include "ir/mod.h"
#include "lang/fn.h"
#include "lang/prelude.h"
#include "sema/call.h"
#include "sema/infer.h"
#include "transform/clone.h"
#include "transform/nested.h"

namespace joggle {
namespace {

bool contains(const std::vector<Val>& values, const Val& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool contains(const std::vector<Op>& values, const Op& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool has_effect(const Type& type) {
  const auto symbol = type.schema().symbol();
  if (symbol.mod_name() == detail::prelude_mod_name &&
      symbol.local_name() == "effect") {
    return true;
  }
  if (symbol.mod_name() != detail::prelude_mod_name ||
      symbol.local_name() != "callable") {
    return false;
  }
  const auto inputs = type.get<std::vector<Type>>("inputs");
  const auto results = type.get<std::vector<Type>>("results");
  return (inputs && std::any_of(inputs->begin(), inputs->end(), has_effect)) ||
         (results && std::any_of(results->begin(), results->end(), has_effect));
}

bool has_effect(const Fn& fn) {
  for (const Val& argument : fn.arguments()) {
    if (has_effect(argument.type())) {
      return true;
    }
  }
  for (const Type& result : fn.result_types()) {
    if (has_effect(result)) {
      return true;
    }
  }
  for (const Op& op : fn.ops()) {
    if (has_effect(op.callee().type()) || op.results().empty()) {
      return true;
    }
  }
  return false;
}

std::optional<std::size_t> result_index(const Op& op, const Val& value) {
  const auto results = op.results();
  const auto found = std::find(results.begin(), results.end(), value);
  return found == results.end()
             ? std::nullopt
             : std::optional<std::size_t>{static_cast<std::size_t>(
                   std::distance(results.begin(), found))};
}

struct Match {
  detail::ValMap arguments;
  std::vector<std::pair<Op, Op>> ops;
  Val root;
  Fn equation;
};

std::optional<Val> bound(const detail::ValMap& values, const Val& pattern) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto& item) { return item.first == pattern; });
  return found == values.end() ? std::nullopt
                               : std::optional<Val>{found->second};
}

bool match_value(const Val& pattern, const Val& target, Match& match,
                 const Blk& root_block);

bool match_call(const Op& pattern, const Op& target, Match& match,
                const Blk& root_block) {
  const auto existing =
      std::find_if(match.ops.begin(), match.ops.end(),
                   [&](const auto& item) { return item.first == pattern; });
  if (existing != match.ops.end()) {
    return existing->second == target;
  }
  if (target.parent() != root_block ||
      pattern.arguments().size() != target.arguments().size() ||
      pattern.results().size() != target.results().size()) {
    return false;
  }
  match.ops.emplace_back(pattern, target);
  if (!match_value(pattern.callee(), target.callee(), match, root_block)) {
    return false;
  }
  const auto pattern_arguments = pattern.arguments();
  const auto target_arguments = target.arguments();
  for (std::size_t index = 0; index < pattern_arguments.size(); ++index) {
    if (!match_value(pattern_arguments[index], target_arguments[index], match,
                     root_block)) {
      return false;
    }
  }
  return true;
}

bool match_reference(const Val& pattern, const Val& target) {
  const auto pattern_fn = pattern.referenced_fn();
  const auto target_fn = target.referenced_fn();
  if (!pattern_fn || !target_fn || *pattern_fn != *target_fn ||
      pattern.bindings().size() != target.bindings().size()) {
    return false;
  }
  const auto left = pattern.bindings();
  const auto right = target.bindings();
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].first != right[index].first ||
        left[index].second != right[index].second) {
      return false;
    }
  }
  return true;
}

bool match_value(const Val& pattern, const Val& target, Match& match,
                 const Blk& root_block) {
  if (pattern.type() != target.type()) {
    return false;
  }
  if (pattern.is_fn_arg()) {
    if (const auto existing = bound(match.arguments, pattern)) {
      return *existing == target;
    }
    match.arguments.emplace_back(pattern, target);
    return true;
  }
  if (pattern.known() || target.known()) {
    return pattern == target;
  }
  if (pattern.referenced_fn()) {
    return match_reference(pattern, target);
  }
  if (pattern.inline_fn()) {
    return false;
  }
  const auto pattern_op = pattern.defining_op();
  const auto target_op = target.defining_op();
  if (!pattern_op || !target_op) {
    return false;
  }
  const auto pattern_result = result_index(*pattern_op, pattern);
  const auto target_result = result_index(*target_op, target);
  return pattern_result && target_result && *pattern_result == *target_result &&
         match_call(*pattern_op, *target_op, match, root_block);
}

bool mark(std::vector<Val>& live, const Val& value) {
  if (contains(live, value)) {
    return false;
  }
  live.push_back(value);
  for (const Val& capture : value.captures()) {
    mark(live, capture);
  }
  return true;
}

std::optional<std::size_t> erase_dead(Fn& fn, Diag& diagnostics) {
  std::vector<Val> live;
  for (const Blk& block : fn.blks()) {
    const Term term = block.terminator();
    if (const auto condition = term.condition()) {
      mark(live, *condition);
    }
    for (const Val& value : term.returned()) {
      mark(live, value);
    }
    for (std::size_t successor = 0; successor < term.successor_count();
         ++successor) {
      for (const Val& value : term.arguments(successor)) {
        mark(live, value);
      }
    }
  }

  std::vector<Op> dead;
  const auto operations = fn.ops();
  for (auto item = operations.rbegin(); item != operations.rend(); ++item) {
    const auto results = item->results();
    const bool required =
        results.empty() || has_effect(item->callee().type()) ||
        std::any_of(results.begin(), results.end(),
                    [&](const Val& result) { return contains(live, result); });
    if (!required) {
      dead.push_back(*item);
      continue;
    }
    mark(live, item->callee());
    for (const Val& argument : item->arguments()) {
      mark(live, argument);
    }
  }
  if (dead.empty()) {
    return std::size_t{0};
  }
  auto edit = fn.edit();
  for (const Op& op : dead) {
    edit.erase(op);
  }
  if (!edit.commit(diagnostics)) {
    return std::nullopt;
  }
  return dead.size();
}

bool valid_equation(const Fn& equation, Diag& diagnostics) {
  if (equation.blks().size() != 1U ||
      equation.entry().terminator().kind() != Term::Kind::Return) {
    diagnostics.report("pass equations must be single-block fns");
    return false;
  }
  const auto returned = equation.entry().terminator().returned();
  const auto results = equation.result_types();
  if (returned.size() != 2U || results.size() != 2U ||
      results.front() != results.back()) {
    diagnostics.report(
        "a pass equation must return two values of the same Type");
    return false;
  }
  if (has_effect(equation)) {
    diagnostics.report("pass equations cannot contain effects");
    return false;
  }
  if (!returned.front().defining_op()) {
    diagnostics.report("a pass pattern must return an expression");
    return false;
  }
  return true;
}

std::optional<Mod::TypeDecl> referenced_type(Compiler& compiler,
                                             const Mod& scope,
                                             std::string_view reference) {
  const std::size_t dot = reference.find('.');
  std::string owner(scope.name());
  std::string_view local = reference;
  if (dot != std::string_view::npos) {
    const std::string_view prefix = reference.substr(0U, dot);
    local = reference.substr(dot + 1U);
    owner = std::string(prefix);
    const auto imported = std::find_if(
        scope.imports().begin(), scope.imports().end(),
        [&](const Mod::Import& import) { return import.prefix() == prefix; });
    if (imported != scope.imports().end()) {
      owner = imported->name;
    }
  }
  const auto mod = compiler.mod(owner);
  auto result = mod ? mod->type(local) : std::optional<Mod::TypeDecl>{};
  if (!result && dot == std::string_view::npos) {
    const auto prelude = compiler.mod(detail::prelude_mod_name);
    result = prelude ? prelude->type(local) : std::optional<Mod::TypeDecl>{};
  }
  return result;
}

bool declares_effect(Compiler& compiler, const Mod& scope,
                     const Mod::Expr& expression) {
  if (expression.kind == Mod::Expr::Kind::Reference) {
    const auto type = referenced_type(compiler, scope, expression.text);
    if (type && type->symbol().mod_name() == detail::prelude_mod_name &&
        type->symbol().local_name() == "effect") {
      return true;
    }
  }
  return std::any_of(expression.arguments.begin(), expression.arguments.end(),
                     [&](const Mod::Expr& argument) {
                       return declares_effect(compiler, scope, argument);
                     });
}

std::vector<Mod::FnDecl> equations(Compiler& compiler, const Mod& laws,
                                   Diag& diagnostics) {
  std::vector<Mod::FnDecl> result;
  for (const Mod::FnDecl& fn : laws.fns()) {
    if (fn.form() != Mod::FnDecl::Form::Body || fn.results().size() != 2U ||
        fn.results().front().domain != fn.results().back().domain) {
      continue;
    }
    const bool effectful =
        std::any_of(fn.inputs().begin(), fn.inputs().end(),
                    [&](const Mod::ParamDecl& input) {
                      return declares_effect(compiler, laws, input.domain);
                    }) ||
        std::any_of(fn.results().begin(), fn.results().end(),
                    [&](const Mod::ParamDecl& output) {
                      return declares_effect(compiler, laws, output.domain);
                    });
    if (effectful) {
      diagnostics.report("pass equations cannot contain effects: '" +
                         fn.symbol().qualified_name() + "'");
      return {};
    }
    if (!detail::compiler_inputs(fn).empty() ||
        !detail::compiler_results(fn).empty() ||
        detail::value_results(fn).size() != 2U) {
      diagnostics.report("pass equation '" + fn.symbol().qualified_name() +
                         "' must use only value ports");
      return {};
    }
    result.push_back(fn);
  }
  if (result.empty()) {
    diagnostics.report("pass Mod '" + std::string(laws.name()) +
                       "' contains no two-result equation fn");
  }
  return result;
}

class PatternProbe {
public:
  PatternProbe(Compiler& compiler, const Mod::FnDecl& law)
      : compiler_(compiler), law_(law), inputs_(detail::value_inputs(law)),
        types_(inputs_.size()) {}

  std::optional<std::vector<std::optional<Type>>>
  run(const Mod::Expr& expression, const Val& target) {
    if (!match(expression, target)) {
      return std::nullopt;
    }
    return types_;
  }

private:
  std::optional<std::size_t> input(std::string_view name) const {
    const auto found =
        std::find_if(inputs_.begin(), inputs_.end(),
                     [&](const auto& value) { return value.name == name; });
    return found == inputs_.end()
               ? std::nullopt
               : std::optional<std::size_t>{static_cast<std::size_t>(
                     std::distance(inputs_.begin(), found))};
  }

  bool bind(std::size_t index, const Val& target) {
    if (types_[index]) {
      return *types_[index] == target.type();
    }
    types_[index] = target.type();
    return true;
  }

  bool match_dynamic(const Mod::Expr& expression, const Op& target,
                     std::size_t callee) {
    if (!bind(callee, target.callee()) ||
        expression.arguments.size() != target.arguments().size()) {
      return false;
    }
    if (std::any_of(expression.labels.begin(), expression.labels.end(),
                    [](const auto& label) { return !label.empty(); })) {
      return false;
    }
    const auto arguments = target.arguments();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      if (!match(expression.arguments[index], arguments[index])) {
        return false;
      }
    }
    return true;
  }

  std::vector<Mod::FnDecl> declarations(const Mod::Expr& expression) const {
    using Kind = Mod::Expr::Kind;
    if (expression.kind == Kind::Call) {
      return detail::visible_fns(compiler_, law_.symbol().mod_name(),
                                 expression.text);
    }
    const auto fixity =
        expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
        : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                           : Mod::FnDecl::Fixity::Infix;
    return detail::visible_operators(compiler_, law_.symbol().mod_name(),
                                     expression.text, fixity);
  }

  bool match_declared(const Mod::Expr& expression, const Op& target) {
    const auto target_fn = target.callee().referenced_fn();
    if (!target_fn) {
      return false;
    }
    const auto visible = declarations(expression);
    if (std::find(visible.begin(), visible.end(), *target_fn) ==
        visible.end()) {
      return false;
    }
    const auto shaped = detail::call_candidate(*target_fn, expression);
    if (!shaped || shaped->parameters.size() != expression.arguments.size()) {
      return false;
    }

    std::vector<std::vector<Val>> target_arguments(target_fn->inputs().size());
    const auto arguments = target.arguments();
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const std::size_t parameter =
          detail::FnAccess::argument_parameter(target, index);
      if (parameter >= target_arguments.size()) {
        return false;
      }
      target_arguments[parameter].push_back(arguments[index]);
    }
    std::vector<std::size_t> occurrences(target_fn->inputs().size());
    for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
      const std::size_t parameter = shaped->parameters[index];
      if (!detail::is_value_port(target_fn->inputs()[parameter])) {
        continue;
      }
      const std::size_t occurrence = occurrences[parameter]++;
      if (occurrence >= target_arguments[parameter].size() ||
          !match(expression.arguments[index],
                 target_arguments[parameter][occurrence])) {
        return false;
      }
    }
    for (std::size_t parameter = 0; parameter < occurrences.size();
         ++parameter) {
      if (occurrences[parameter] != target_arguments[parameter].size()) {
        return false;
      }
    }
    return true;
  }

  bool match(const Mod::Expr& expression, const Val& target) {
    using Kind = Mod::Expr::Kind;
    if (expression.kind == Kind::Variable) {
      const auto parameter = input(expression.text);
      return parameter && bind(*parameter, target);
    }
    const bool call =
        expression.kind == Kind::Call || expression.kind == Kind::Prefix ||
        expression.kind == Kind::Infix || expression.kind == Kind::Postfix;
    if (!call) {
      return target.known();
    }
    const auto operation = target.defining_op();
    if (!operation || operation->results().size() != 1U ||
        operation->result(0) != target) {
      return false;
    }
    if (expression.kind == Kind::Call) {
      if (const auto callee = input(expression.text)) {
        return match_dynamic(expression, *operation, *callee);
      }
    }
    return match_declared(expression, *operation);
  }

  Compiler& compiler_;
  Mod::FnDecl law_;
  std::vector<Mod::ParamDecl> inputs_;
  std::vector<std::optional<Type>> types_;
};

std::optional<std::vector<std::optional<Type>>>
pattern_types(Compiler& compiler, const Mod& laws, const Mod::FnDecl& law,
              const Val& target) {
  const auto body = detail::ModAccess::body(laws, law);
  if (!body || body->blocks.size() != 1U || body->blocks.front().terminator ||
      body->blocks.front().statements.empty()) {
    return std::nullopt;
  }
  const detail::StatementSyntax& statement =
      body->blocks.front().statements.back();
  if (statement.kind != detail::StatementSyntax::Kind::Return ||
      statement.values.size() != 2U) {
    return std::nullopt;
  }
  return PatternProbe(compiler, law)
      .run(statement.values.front().value, target);
}

std::optional<Fn>
materialize_equation(Compiler& compiler, const Mod::FnDecl& law,
                     const Type& root_type,
                     std::vector<std::optional<Type>> arguments = {}) {
  if (arguments.empty()) {
    arguments.resize(detail::value_inputs(law).size());
  }
  std::vector<std::optional<detail::ParamVal>> known;
  std::vector<std::optional<Type>> expected(2U);
  expected.front() = root_type;
  Diag attempt;
  const auto types = detail::resolve_partial_call_types(
      compiler, law, arguments, known, expected, attempt, std::nullopt, false);
  if (!types || types->results.size() != 2U ||
      types->results.front() != types->results.back()) {
    return std::nullopt;
  }

  auto holder = compiler.create_fn();
  if (!holder) {
    return std::nullopt;
  }
  auto edit = holder->edit();
  std::vector<Val> inputs;
  inputs.reserve(types->arguments.size());
  for (const Type& type : types->arguments) {
    inputs.push_back(edit.argument(type));
  }
  Op call = edit.call(law, std::move(inputs), types->results);
  edit.ret(holder->entry(), call.results());
  if (!edit.commit(compiler, attempt)) {
    return std::nullopt;
  }
  return compiler.materialize(call, attempt);
}

using Specializations = std::vector<std::pair<Type, std::optional<Fn>>>;

std::optional<Fn> specialize(Compiler& compiler, const Mod::FnDecl& law,
                             const Type& root_type,
                             Specializations& specializations) {
  const auto existing =
      std::find_if(specializations.begin(), specializations.end(),
                   [&](const auto& item) { return item.first == root_type; });
  if (existing != specializations.end()) {
    return existing->second;
  }
  auto equation = materialize_equation(compiler, law, root_type);
  specializations.emplace_back(root_type, equation);
  return equation;
}

std::optional<std::size_t> apply_equation(Compiler& compiler, Fn& fn,
                                          const Mod& laws,
                                          const Mod::FnDecl& law,
                                          Specializations& specializations,
                                          Diag& diagnostics) {
  struct Nested {
    Val value;
    Fn body;
  };
  std::vector<Nested> nested;
  std::size_t total = 0;
  for (const Val& value : detail::nested_values(fn)) {
    auto body = value.inline_fn();
    if (!body) {
      continue;
    }
    const auto changed = apply_equation(compiler, *body, laws, law,
                                        specializations, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    if (*changed != 0U) {
      total += *changed;
      nested.push_back({value, std::move(*body)});
    }
  }
  if (!nested.empty()) {
    auto edit = fn.edit();
    for (Nested& item : nested) {
      Val replacement = edit.callable(std::move(item.body), item.value.type(),
                                      item.value.captures());
      edit.replace(item.value, replacement);
    }
    if (!edit.commit(compiler, diagnostics)) {
      return std::nullopt;
    }
  }

  std::vector<Match> matches;
  std::vector<Op> claimed;
  for (const Op& operation : fn.ops()) {
    for (const Val& candidate : operation.results()) {
      auto equation =
          specialize(compiler, law, candidate.type(), specializations);
      if (!equation) {
        const auto arguments = pattern_types(compiler, laws, law, candidate);
        if (arguments) {
          equation =
              materialize_equation(compiler, law, candidate.type(), *arguments);
        }
      }
      if (!equation) {
        continue;
      }
      if (!valid_equation(*equation, diagnostics)) {
        return std::nullopt;
      }
      const Val pattern_root =
          equation->entry().terminator().returned().front();
      Match match{{}, {}, candidate, std::move(*equation)};
      if (!match_value(pattern_root, candidate, match, operation.parent())) {
        continue;
      }
      const bool overlaps = std::any_of(
          match.ops.begin(), match.ops.end(),
          [&](const auto& item) { return contains(claimed, item.second); });
      if (overlaps) {
        continue;
      }
      for (const auto& item : match.ops) {
        claimed.push_back(item.second);
      }
      matches.push_back(std::move(match));
    }
  }
  if (matches.empty()) {
    return total;
  }

  auto edit = fn.edit();
  for (Match& match : matches) {
    detail::ValMap values;
    for (const Val& argument : match.equation.arguments()) {
      const auto value = bound(match.arguments, argument);
      if (!value) {
        diagnostics.report("a pass replacement uses an unbound argument");
        return std::nullopt;
      }
      values.emplace_back(argument, *value);
    }
    const Op root = *match.root.defining_op();
    const Val replacement =
        match.equation.entry().terminator().returned().back();
    const auto cloned = detail::clone_before(edit, replacement, root, values,
                                             diagnostics, root.location());
    if (!cloned) {
      return std::nullopt;
    }
    edit.replace(match.root, *cloned);
  }
  if (!edit.commit(compiler, diagnostics)) {
    return std::nullopt;
  }
  if (!erase_dead(fn, diagnostics)) {
    return std::nullopt;
  }
  return total + matches.size();
}

}  // namespace

std::optional<std::size_t> apply_pass(Compiler& compiler, Fn& fn,
                                      const Mod& laws, Diag& diagnostics) {
  const auto rules = equations(compiler, laws, diagnostics);
  if (rules.empty()) {
    return std::nullopt;
  }
  Fn candidate = fn;
  std::size_t total = 0;
  for (const Mod::FnDecl& rule : rules) {
    Specializations specializations;
    const auto changed = apply_equation(compiler, candidate, laws, rule,
                                        specializations, diagnostics);
    if (!changed) {
      return std::nullopt;
    }
    total += *changed;
  }
  if (total != 0U) {
    fn = std::move(candidate);
  }
  return total;
}

}  // namespace joggle
