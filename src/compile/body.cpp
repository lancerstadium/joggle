#include "lang/fn.h"

#include "base/diag.h"
#include "compile/compiler.h"
#include "compile/eval.h"
#include "ir/fn.h"
#include "ir/mod.h"
#include "ir/type.h"
#include "joggle/compiler.h"
#include "lang/expr.h"
#include "lang/prelude.h"
#include "sema/call.h"
#include "sema/domain.h"
#include "sema/infer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace joggle {
namespace {

using detail::ParamVal;

std::pair<std::string_view, std::string_view>
split_reference(std::string_view owner, std::string_view reference) {
  const std::size_t dot = reference.find('.');
  return dot == std::string_view::npos
             ? std::pair<std::string_view, std::string_view>{owner, reference}
             : std::pair<std::string_view, std::string_view>{
                   reference.substr(0, dot), reference.substr(dot + 1U)};
}

class Instantiator {
  struct Path {
    Blk block;
    detail::Locals locals;
    std::size_t residual_depth = 0;
  };

  struct Flow {
    std::vector<Path> next;
    std::vector<Path> breaks;
    std::vector<Path> continues;

    void append(Flow&& flow) {
      next.insert(next.end(), std::make_move_iterator(flow.next.begin()),
                  std::make_move_iterator(flow.next.end()));
      breaks.insert(breaks.end(), std::make_move_iterator(flow.breaks.begin()),
                    std::make_move_iterator(flow.breaks.end()));
      continues.insert(continues.end(),
                       std::make_move_iterator(flow.continues.begin()),
                       std::make_move_iterator(flow.continues.end()));
    }
  };

  struct LoopContext {
    std::optional<Blk> continue_target;
    std::optional<Blk> break_target;
    std::vector<std::string> carried_names;
    std::vector<Type> carried_types;
  };

  struct PendingArgument {
    std::optional<detail::StagedVal> value;
    std::string fn;
    std::optional<Mod::Expr> inline_fn;
    std::optional<Mod::Expr> expression;
    std::optional<std::vector<Type>> inline_inputs;
    std::optional<Type> inferred_type;
    std::size_t order = 0;

    bool is_fn() const { return !fn.empty(); }
    bool is_inline_fn() const { return inline_fn.has_value(); }
    bool is_expression() const { return expression.has_value(); }
    bool valid() const {
      return value.has_value() || is_fn() || is_expression() ||
             (is_inline_fn() && inline_inputs.has_value());
    }
  };

  struct PendingCall {
    Mod::FnDecl fn;
    std::vector<std::vector<PendingArgument>> arguments;
    detail::CallTypes partial_types;
    std::vector<std::optional<ParamVal>> known_arguments;
  };

public:
  Instantiator(Compiler& compiler, Mod::FnDecl fn, const detail::FnBody& body,
               Diag& diagnostics, std::vector<Val> known_arguments,
               detail::KnownBindings bindings)
      : compiler_(compiler), declaration_(std::move(fn)), body_(body),
        owner_(declaration_->symbol().mod_name()), diagnostics_(diagnostics),
        initial_diagnostics_(diagnostics.size()),
        supplied_known_(std::move(known_arguments)),
        supplied_bindings_(std::move(bindings)) {}

  Instantiator(Compiler& compiler, std::string owner,
               const detail::FnBody& body, Diag& diagnostics,
               std::vector<std::pair<std::string, Type>> arguments,
               std::optional<std::vector<Type>> results,
               detail::KnownBindings bindings)
      : compiler_(compiler), body_(body), owner_(std::move(owner)),
        diagnostics_(diagnostics), initial_diagnostics_(diagnostics.size()),
        supplied_bindings_(std::move(bindings)),
        inline_arguments_(std::move(arguments)),
        inline_results_(std::move(results)) {}

  std::optional<Fn> instantiate() {
    if (!declaration_) {
      return instantiate_inline();
    }
    fn_ = compiler_.create_fn();
    if (!fn_) {
      return std::nullopt;
    }
    const auto& contract = detail::FnTypeAccess::get(*declaration_);
    const auto parameters = declaration_->inputs();
    std::vector<std::optional<detail::StagedVal>> known_parameters(
        parameters.size());
    detail::KnownBindings bindings = supplied_bindings_;
    std::size_t supplied = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        continue;
      }
      const Mod::ParamDecl& parameter = parameters[index];
      std::size_t required_after = 0;
      for (std::size_t next = index + 1U; next < parameters.size(); ++next) {
        if (!detail::is_value_port(parameters[next]) &&
            !parameters[next].default_value) {
          ++required_after;
        }
      }
      const bool use_default =
          parameter.default_value &&
          supplied_known_.size() - supplied == required_after;
      std::optional<detail::StagedVal> value;
      if (!use_default && supplied < supplied_known_.size()) {
        value = detail::stage(supplied_known_[supplied++]);
      } else if (parameter.default_value) {
        auto payload = detail::parameter_default(parameter);
        auto execution = payload ? detail::exec_val(*payload, parameter)
                                 : std::optional<detail::ExecVal>{};
        value = execution ? detail::stage(compiler_, std::move(*execution))
                          : std::optional<detail::StagedVal>{};
      }
      const detail::ExecVal* known =
          value && value->known() ? value->known_value() : nullptr;
      const auto payload =
          known ? detail::parameter_value(*known) : std::optional<ParamVal>{};
      if (!value || !payload ||
          !detail::matches_parameter(parameter, *payload)) {
        report("fn specialization needs a compatible Known argument '" +
                   parameter.name + "'",
               body_.range);
        continue;
      }
      known_parameters[index] = *value;
      bindings.insert_or_assign(
          parameter.name, detail::KnownBinding{*payload, parameter.domain});
      if (index < contract.bindings.size() && contract.bindings[index] &&
          contract.bindings[index]->kind == Mod::Expr::Kind::Variable) {
        bindings.insert_or_assign(
            contract.bindings[index]->text,
            detail::KnownBinding{*payload, parameter.domain});
      }
    }
    if (supplied != supplied_known_.size()) {
      report("fn specialization has too many Known arguments", body_.range);
    }
    if (!ok()) {
      return std::nullopt;
    }

    const auto resolve_type =
        [&](const Mod::Expr& expression,
            std::string_view role) -> std::optional<Type> {
      const Mod::ParamDecl expected{
          std::string(role), detail::domain_expression(detail::ValKind::Type),
          false, std::nullopt};
      auto value = detail::evaluate_known_expression(
          compiler_, owner_, expression, expected, bindings, diagnostics_,
          source(body_.range));
      const Type* type = value ? value->as_type() : nullptr;
      if (type == nullptr) {
        report("cannot resolve " + std::string(role) +
                   " type during "
                   "fn specialization",
               body_.range);
        return std::nullopt;
      }
      return *type;
    };

    result_types_.clear();
    const auto results = detail::value_results(*declaration_);
    for (const auto& result : results) {
      if (auto result_type = resolve_type(result.domain, "result")) {
        result_types_.push_back(*result_type);
      }
    }
    const auto constrain_returns = [&](const auto& self,
                                       const auto& statements) -> void {
      for (const detail::StatementSyntax& statement : statements) {
        if (statement.kind == detail::StatementSyntax::Kind::Return &&
            statement.values.size() == result_types_.size()) {
          for (std::size_t index = 0; index < statement.values.size();
               ++index) {
            const auto& expression = statement.values[index];
            if ((expression.value.kind != Mod::Expr::Kind::Reference &&
                 expression.value.kind != Mod::Expr::Kind::Variable) ||
                !expression.value.arguments.empty()) {
              continue;
            }
            const auto [found, inserted] = expected_values_.emplace(
                expression.value.text, result_types_[index]);
            if (!inserted && found->second != result_types_[index]) {
              report("one returned value is constrained to different types",
                     expression.range);
            }
          }
        }
        self(self, statement.body);
        self(self, statement.otherwise);
      }
    };
    for (const detail::BlkSyntax& block : body_.blocks) {
      if (block.terminator &&
          block.terminator->kind == detail::TermSyntax::Kind::Return &&
          block.terminator->values.size() == result_types_.size()) {
        for (std::size_t index = 0; index < block.terminator->values.size();
             ++index) {
          const auto& expression = block.terminator->values[index];
          if ((expression.value.kind != Mod::Expr::Kind::Reference &&
               expression.value.kind != Mod::Expr::Kind::Variable) ||
              !expression.value.arguments.empty()) {
            continue;
          }
          const auto [found, inserted] = expected_values_.emplace(
              expression.value.text, result_types_[index]);
          if (!inserted && found->second != result_types_[index]) {
            report("one returned value is constrained to different types",
                   expression.range);
          }
        }
      }
      constrain_returns(constrain_returns, block.statements);
    }
    std::vector<Type> argument_types;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        if (auto argument_type =
                resolve_type(parameters[index].domain, "input")) {
          argument_types.push_back(*argument_type);
        }
      }
    }
    if (argument_types.size() != detail::value_inputs(*declaration_).size() ||
        result_types_.size() != results.size()) {
      return std::nullopt;
    }

    detail::FnAccess::declare(*fn_, *declaration_, argument_types,
                              result_types_);
    edit_.emplace(fn_->edit());
    locals_.push();
    std::size_t residual = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (detail::is_value_port(parameters[index])) {
        define(parameters[index].name,
               edit_->argument(argument_types[residual++]), body_.range);
      } else if (known_parameters[index]) {
        define_staged(parameters[index].name, *known_parameters[index],
                      body_.range);
      }
    }
    for (const auto& generic : declaration_->generics()) {
      const auto found = bindings.find(generic.name);
      if (found == bindings.end() || locals_.contains(generic.name)) {
        continue;
      }
      const Mod::ParamDecl parameter{generic.name, generic.domain, false,
                                     std::nullopt};
      auto value = detail::exec_val(found->second.value, parameter);
      auto staged = value ? detail::stage(compiler_, std::move(*value))
                          : std::optional<detail::StagedVal>{};
      if (!staged) {
        report("generic '" + generic.name + "' cannot enter staged evaluation",
               body_.range);
        continue;
      }
      define_staged(generic.name, std::move(*staged), body_.range);
    }

    blocks_.emplace("entry", fn_->entry());
    for (std::size_t index = 1; index < body_.blocks.size(); ++index) {
      const detail::BlkSyntax& block = body_.blocks[index];
      std::vector<Type> block_argument_types;
      for (const detail::BlkArgSyntax& argument : block.arguments) {
        if (auto argument_type = type(argument.type)) {
          block_argument_types.push_back(*argument_type);
        }
      }
      if (block_argument_types.size() == block.arguments.size()) {
        blocks_.emplace(block.name,
                        edit_->blk(std::move(block_argument_types)));
      }
    }

    for (const detail::BlkSyntax& block : body_.blocks) {
      const auto ir = blocks_.find(block.name);
      if (ir == blocks_.end()) {
        continue;
      }
      const auto arguments = ir->second.arguments();
      for (std::size_t index = 0;
           index < block.arguments.size() && index < arguments.size();
           ++index) {
        define(block.arguments[index].name, arguments[index],
               block.arguments[index].range);
      }
    }

    for (const detail::BlkSyntax& block : body_.blocks) {
      const auto ir = blocks_.find(block.name);
      if (ir == blocks_.end()) {
        continue;
      }
      Flow current = instantiate_sequence(block.statements, ir->second);
      if (!current.breaks.empty() || !current.continues.empty()) {
        report("loop control escaped its structured loop", block.range);
        continue;
      }
      if (current.next.empty()) {
        continue;
      }
      if (!block.terminator) {
        report("fn path falls through without returning", block.range);
        continue;
      }
      const detail::TermSyntax& terminator = *block.terminator;
      const auto target = [&](std::size_t index) -> std::optional<Blk> {
        if (index >= terminator.successors.size()) {
          return std::nullopt;
        }
        const auto found = blocks_.find(terminator.successors[index].target);
        if (found == blocks_.end()) {
          report("unknown successor block '" +
                     terminator.successors[index].target + "'",
                 terminator.successors[index].range);
          return std::nullopt;
        }
        return found->second;
      };
      for (const Path& active : current.next) {
        restore(active);
        if (terminator.kind == detail::TermSyntax::Kind::Return) {
          instantiate_return(terminator.values, terminator.range, active.block);
          continue;
        }
        std::vector<std::vector<Val>> edge_arguments;
        for (const detail::SuccessorSyntax& successor : terminator.successors) {
          std::vector<Val> values;
          for (const detail::ExprSyntax& argument : successor.arguments) {
            if (auto value = use(argument)) {
              values.push_back(*value);
            }
          }
          edge_arguments.push_back(std::move(values));
        }
        if (terminator.kind == detail::TermSyntax::Kind::Jump) {
          if (auto destination = target(0)) {
            edit_->jump(active.block, *destination,
                        edge_arguments.empty() ? std::vector<Val>{}
                                               : std::move(edge_arguments[0]));
          }
        } else {
          auto condition = terminator.condition ? use(*terminator.condition)
                                                : std::optional<Val>{};
          auto true_target = target(0);
          auto false_target = target(1);
          if (!condition || !true_target || !false_target ||
              edge_arguments.size() != 2U) {
            continue;
          }
          edit_->branch(active.block, *condition, *true_target,
                        std::move(edge_arguments[0]), *false_target,
                        std::move(edge_arguments[1]));
        }
      }
    }
    if (!ok() || !detail::FnAccess::commit(*edit_, compiler_, diagnostics_)) {
      return std::nullopt;
    }
    edit_.reset();
    return compiler_.verify(*fn_) ? std::move(fn_) : std::nullopt;
  }

private:
  std::optional<Fn> instantiate_inline() {
    if (body_.blocks.size() != 1U || body_.blocks.front().name != "entry" ||
        body_.blocks.front().terminator) {
      report("an inline fn must have one structured entry body", body_.range);
      return std::nullopt;
    }
    fn_ = compiler_.create_fn();
    if (!fn_) {
      return std::nullopt;
    }
    std::vector<Type> argument_types;
    argument_types.reserve(inline_arguments_.size());
    for (const auto& [name, type] : inline_arguments_) {
      static_cast<void>(name);
      argument_types.push_back(type);
    }
    if (inline_results_) {
      result_types_ = *inline_results_;
      detail::FnAccess::define(*fn_, argument_types, result_types_);
    }
    edit_.emplace(fn_->edit());
    locals_.push();
    for (const auto& [name, binding] : supplied_bindings_) {
      if (std::any_of(
              inline_arguments_.begin(), inline_arguments_.end(),
              [&](const auto& argument) { return argument.first == name; })) {
        continue;
      }
      const Mod::ParamDecl parameter{name, binding.domain.value_or(Mod::Expr{}),
                                     false, std::nullopt};
      auto value = detail::exec_val(binding.value, parameter);
      auto staged = value ? detail::stage(compiler_, std::move(*value))
                          : std::optional<detail::StagedVal>{};
      if (!staged) {
        report("inline fn cannot inherit compiler binding '" + name + "'",
               body_.range);
      } else {
        define_staged(name, std::move(*staged), body_.range);
      }
    }
    for (std::size_t index = 0; index < inline_arguments_.size(); ++index) {
      define(inline_arguments_[index].first,
             edit_->argument(argument_types[index]), body_.range);
    }
    blocks_.emplace("entry", fn_->entry());
    if (!inline_results_) {
      const auto& statements = body_.blocks.front().statements;
      if (statements.size() != 1U ||
          statements.front().kind != detail::StatementSyntax::Kind::Return ||
          statements.front().values.size() != 1U) {
        report("an inferred inline fn must return one expression", body_.range);
        return std::nullopt;
      }
      const auto& returned = statements.front().values.front();
      auto [tail, value] =
          instantiate_expression(returned.value, returned.range, fn_->entry());
      if (!value) {
        return std::nullopt;
      }
      edit_->ret(tail, {*value});
    } else {
      Flow flow =
          instantiate_sequence(body_.blocks.front().statements, fn_->entry());
      if (!flow.next.empty()) {
        report("inline fn path falls through without returning", body_.range);
      }
      if (!flow.breaks.empty() || !flow.continues.empty()) {
        report("loop control escaped an inline fn", body_.range);
      }
    }
    if (!ok() || !detail::FnAccess::commit(*edit_, compiler_, diagnostics_)) {
      return std::nullopt;
    }
    edit_.reset();
    return compiler_.verify(*fn_) ? std::move(fn_) : std::nullopt;
  }

  Path path(Blk block) const {
    return {std::move(block), locals_, residual_control_depth_};
  }

  Flow next(Blk block) const {
    Flow flow;
    flow.next.push_back(path(std::move(block)));
    return flow;
  }

  Flow transfer(bool is_break, Blk block) const {
    Flow flow;
    auto& paths = is_break ? flow.breaks : flow.continues;
    paths.push_back(path(std::move(block)));
    return flow;
  }

  void restore(const Path& active) {
    locals_ = active.locals;
    residual_control_depth_ = active.residual_depth;
  }

  static void trim_scopes(Flow& flow, std::size_t depth) {
    const auto trim = [&](std::vector<Path>& paths) {
      for (Path& path : paths) {
        path.locals.resize(depth);
      }
    };
    trim(flow.next);
    trim(flow.breaks);
    trim(flow.continues);
  }

  bool equivalent_staged_value(const detail::StagedVal& lhs,
                               const detail::StagedVal& rhs) const {
    if (detail::same_staged_value(lhs, rhs)) {
      return true;
    }
    if (lhs.known() || rhs.known()) {
      return false;
    }
    const Val* left_value = lhs.residual_value();
    const Val* right_value = rhs.residual_value();
    if (left_value == nullptr || right_value == nullptr) {
      return false;
    }
    const auto left = left_value->defining_op();
    const auto right = right_value->defining_op();
    const auto left_callee =
        left ? left->callee().referenced_fn() : std::optional<Mod::FnDecl>{};
    const auto right_callee =
        right ? right->callee().referenced_fn() : std::optional<Mod::FnDecl>{};
    if (!left || !right || !left_callee || !right_callee ||
        left_callee->symbol() != right_callee->symbol()) {
      return false;
    }
    const auto callee = *left_callee;
    if (callee.name() != "literal" ||
        detail::compiler_inputs(callee).size() != 1U ||
        !detail::value_inputs(callee).empty() ||
        !detail::compiler_results(callee).empty() ||
        detail::value_results(callee).size() != 1U) {
      return false;
    }
    const auto left_results = left->results();
    const auto right_results = right->results();
    const auto left_result =
        std::find(left_results.begin(), left_results.end(), *left_value);
    const auto right_result =
        std::find(right_results.begin(), right_results.end(), *right_value);
    if (left_result == left_results.end() ||
        right_result == right_results.end() ||
        std::distance(left_results.begin(), left_result) !=
            std::distance(right_results.begin(), right_result)) {
      return false;
    }
    return left->arguments() == right->arguments() &&
           left->callee().bindings() == right->callee().bindings();
  }

  bool same_staged_state(std::span<const detail::StagedVal> lhs,
                         std::span<const detail::StagedVal> rhs) const {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [&](const detail::StagedVal& left,
                          const detail::StagedVal& right) {
                        return equivalent_staged_value(left, right);
                      });
  }

  Flow instantiate_sequence(std::span<const detail::StatementSyntax> statements,
                            Blk block) {
    Flow current = next(std::move(block));
    for (const detail::StatementSyntax& statement : statements) {
      Flow following;
      following.breaks = std::move(current.breaks);
      following.continues = std::move(current.continues);
      for (const Path& active : current.next) {
        restore(active);
        following.append(instantiate_statement(statement, active.block));
      }
      current = std::move(following);
      if (current.next.empty()) {
        break;
      }
    }
    return current;
  }

  bool ok() const { return diagnostics_.size() == initial_diagnostics_; }

  Loc source(detail::SyntaxRange range) const {
    return {body_.source, range.begin, range.end};
  }

  void report(std::string message, detail::SyntaxRange range) {
    diagnostics_.report(std::move(message), source(range));
  }

  std::optional<std::string_view>
  resolve_prefix(std::string_view from, std::string_view prefix) const {
    if (prefix == detail::prelude_mod_name) {
      return detail::prelude_mod_name;
    }
    if (prefix == from) {
      return from;
    }
    const auto owner = compiler_.mod(from);
    if (!owner) {
      return std::nullopt;
    }
    const auto found = std::find_if(
        owner->imports().begin(), owner->imports().end(),
        [&](const Mod::Import& import) { return import.prefix() == prefix; });
    return found == owner->imports().end()
               ? std::nullopt
               : std::optional<std::string_view>{found->name};
  }

  template <typename Declaration>
  std::optional<Declaration> declaration(std::string_view reference,
                                         detail::SyntaxRange range,
                                         std::string_view scope = {}) {
    if (scope.empty()) {
      scope = owner_;
    }
    const bool prelude_type = std::is_same_v<Declaration, Mod::TypeDecl> &&
                              detail::is_prelude_type(reference);
    const std::string qualified = prelude_type
                                      ? std::string(detail::prelude_mod_name) +
                                            "." + std::string(reference)
                                      : std::string(reference);
    const auto [prefix, local] = split_reference(scope, qualified);
    const auto mod_name = resolve_prefix(scope, prefix);
    if (!mod_name) {
      report("reference '" + std::string(reference) +
                 "' is not local or directly imported",
             range);
      return std::nullopt;
    }
    const auto mod = compiler_.mod(*mod_name);
    if (!mod) {
      report("unknown mod '" + std::string(*mod_name) + "'", range);
      return std::nullopt;
    }
    std::optional<Declaration> result;
    if constexpr (std::is_same_v<Declaration, Mod::TypeDecl>) {
      result = mod->type(local);
    } else {
      result = mod->fn(local);
    }
    if (!result) {
      constexpr std::string_view kind =
          std::is_same_v<Declaration, Mod::TypeDecl> ? "type" : "fn";
      report("unknown " + std::string(kind) + " '" + std::string(reference) +
                 "'",
             range);
    }
    return result;
  }

  std::optional<Mod::Symbol::Kind>
  declaration_kind(std::string_view reference) const {
    const bool prelude_type = detail::is_prelude_type(reference);
    const std::string qualified = prelude_type
                                      ? std::string(detail::prelude_mod_name) +
                                            "." + std::string(reference)
                                      : std::string(reference);
    const auto [prefix, local] = split_reference(owner_, qualified);
    const auto mod_name = resolve_prefix(owner_, prefix);
    const auto mod = mod_name ? compiler_.mod(*mod_name) : std::optional<Mod>{};
    if (!mod) {
      return std::nullopt;
    }
    if (mod->type(local)) {
      return Mod::Symbol::Kind::Type;
    }
    return std::nullopt;
  }

  std::optional<Mod::ParamDecl> known_result(const Mod::Expr& expression,
                                             detail::SyntaxRange range) {
    using Kind = Mod::Expr::Kind;
    if (expression.kind == Kind::Evaluate) {
      return expression.arguments.size() == 1U
                 ? known_result(expression.arguments.front(), range)
                 : std::nullopt;
    }
    if (expression.kind == Kind::Variable ||
        expression.kind == Kind::Reference) {
      if (!expression.arguments.empty()) {
        const auto kind = declaration_kind(expression.text);
        if (kind == Mod::Symbol::Kind::Type) {
          return Mod::ParamDecl{
              "result", detail::domain_expression(detail::ValKind::Type), false,
              std::nullopt};
        }
      }
      auto value = lookup_staged(expression.text);
      auto value_domain =
          value ? detail::type_domain(value->type()) : std::nullopt;
      return value_domain
                 ? std::optional<Mod::ParamDecl>{{"result",
                                                  std::move(*value_domain),
                                                  false, std::nullopt}}
                 : std::nullopt;
    }
    if (expression.kind == Kind::Number) {
      const bool real =
          expression.text.find_first_of(".eE") != std::string::npos;
      return Mod::ParamDecl{
          "result",
          detail::domain_expression(real ? detail::ValKind::Real
                                         : detail::ValKind::Integer),
          false, std::nullopt};
    }
    if (expression.kind == Kind::Boolean) {
      return Mod::ParamDecl{"result",
                            detail::domain_expression(detail::ValKind::Boolean),
                            false, std::nullopt};
    }
    if (expression.kind == Kind::String) {
      return Mod::ParamDecl{"result",
                            detail::domain_expression(detail::ValKind::String),
                            false, std::nullopt};
    }
    if (expression.kind == Kind::FnType) {
      return Mod::ParamDecl{"result",
                            detail::domain_expression(detail::ValKind::Type),
                            false, std::nullopt};
    }
    if (expression.kind == Kind::List && !expression.arguments.empty()) {
      auto element = known_result(expression.arguments.front(), range);
      return element ? std::optional<Mod::ParamDecl>{{"result",
                                                      Mod::Expr::list_domain(
                                                          element->domain),
                                                      false, std::nullopt}}
                     : std::nullopt;
    }
    if (expression.kind == Kind::If && expression.arguments.size() == 3U) {
      return known_result(expression.arguments[1], range);
    }
    if (expression.kind == Kind::Call) {
      std::vector<Mod::ParamDecl> matches;
      for (const auto& fn : visible_fns(expression.text)) {
        auto candidate = detail::call_candidate(fn, expression);
        const auto results = detail::compiler_results(fn);
        if (!candidate || !detail::value_inputs(fn).empty() ||
            !detail::value_results(fn).empty() || results.size() != 1U) {
          continue;
        }
        bool accepts = true;
        for (std::size_t index = 0; index < expression.arguments.size();
             ++index) {
          const auto actual = known_result(expression.arguments[index], range);
          if (actual && fn.inputs()[candidate->parameters[index]].domain !=
                            actual->domain) {
            accepts = false;
            break;
          }
        }
        if (accepts) {
          matches.push_back(results.front());
        }
      }
      if (matches.empty()) {
        return std::nullopt;
      }
      return std::all_of(matches.begin() + 1, matches.end(),
                         [&](const Mod::ParamDecl& result) {
                           return result.domain == matches.front().domain;
                         })
                 ? std::optional<Mod::ParamDecl>{matches.front()}
                 : std::nullopt;
    }
    if ((expression.kind == Kind::Prefix || expression.kind == Kind::Infix ||
         expression.kind == Kind::Postfix) &&
        !expression.arguments.empty()) {
      const auto fixity =
          expression.kind == Kind::Prefix    ? Mod::FnDecl::Fixity::Prefix
          : expression.kind == Kind::Postfix ? Mod::FnDecl::Fixity::Postfix
                                             : Mod::FnDecl::Fixity::Infix;
      std::vector<Mod::ParamDecl> matches;
      for (const auto& fn : detail::visible_operators(
               compiler_, owner_, expression.text, fixity)) {
        const auto inputs = detail::compiler_inputs(fn);
        const auto results = detail::compiler_results(fn);
        if (!detail::value_inputs(fn).empty() ||
            !detail::value_results(fn).empty() ||
            inputs.size() != expression.arguments.size() ||
            results.size() != 1U) {
          continue;
        }
        bool accepts = true;
        for (std::size_t index = 0; index < inputs.size(); ++index) {
          const auto actual = known_result(expression.arguments[index], range);
          if (!actual || inputs[index].domain != actual->domain) {
            accepts = false;
            break;
          }
        }
        if (accepts) {
          matches.push_back(results.front());
        }
      }
      if (!matches.empty() && std::all_of(matches.begin() + 1, matches.end(),
                                          [&](const Mod::ParamDecl& result) {
                                            return result.domain ==
                                                   matches.front().domain;
                                          })) {
        return matches.front();
      }
      return known_result(expression.arguments.front(), range);
    }
    return std::nullopt;
  }

  std::optional<detail::StagedVal>
  evaluate_known(const detail::ExprSyntax& syntax,
                 std::optional<Mod::ParamDecl> contextual = std::nullopt) {
    auto expected = contextual ? std::move(contextual)
                               : known_result(syntax.value, syntax.range);
    if (!expected) {
      report("cannot determine the type required by compile-time evaluation",
             syntax.range);
      return std::nullopt;
    }
    auto payload = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, *expected, locals_.known_bindings(),
        diagnostics_, source(syntax.range), residual_control_depth_ == 0U);
    auto value = payload ? detail::exec_val(*payload, *expected)
                         : std::optional<detail::ExecVal>{};
    return value ? detail::stage(compiler_, std::move(*value))
                 : std::optional<detail::StagedVal>{};
  }

  std::optional<Type> type(const detail::ExprSyntax& syntax) {
    const Mod::ParamDecl expected{
        "type", detail::domain_expression(detail::ValKind::Type), false,
        std::nullopt};
    const std::size_t before = diagnostics_.size();
    auto value = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, expected, locals_.known_bindings(),
        diagnostics_, source(syntax.range), residual_control_depth_ == 0U);
    const Type* resolved = value ? value->as_type() : nullptr;
    if (resolved == nullptr && diagnostics_.size() == before) {
      report("type annotation does not evaluate to a Known type", syntax.range);
    }
    return resolved ? std::optional<Type>{*resolved} : std::nullopt;
  }

  std::optional<Val> lookup(std::string_view name) const {
    auto value = lookup_staged(name);
    return value ? detail::ir_value(compiler_, *value) : std::optional<Val>{};
  }

  std::optional<detail::StagedVal> lookup_staged(std::string_view name) const {
    const detail::StagedVal* value = locals_.find(name);
    return value ? std::optional<detail::StagedVal>{*value} : std::nullopt;
  }

  bool declared_local(std::string_view name) const {
    return locals_.contains(name);
  }

  std::vector<Mod::FnDecl> visible_fns(std::string_view reference) const {
    return detail::visible_fns(compiler_, owner_, reference);
  }

  std::optional<PendingCall>
  plan_call(const Mod::FnDecl& fn, const Mod::Expr& expression,
            std::span<const PendingArgument> supplied,
            std::span<const std::optional<Type>> expected,
            detail::SyntaxRange range, bool allow_guarded_evaluation,
            Diag* errors = nullptr) {
    const auto reject = [&](std::string message) {
      if (errors) {
        errors->report(std::move(message), source(range));
      }
    };
    const auto candidate = detail::call_candidate(fn, expression);
    if (!candidate || candidate->parameters.size() != supplied.size()) {
      reject("call arguments do not match '" + fn.signature() + "'");
      return std::nullopt;
    }

    const auto parameters = fn.inputs();
    PendingCall result{
        fn,
        std::vector<std::vector<PendingArgument>>(parameters.size()),
        {},
        std::vector<std::optional<ParamVal>>(
            detail::compiler_inputs(fn).size())};
    for (std::size_t index = 0; index < supplied.size(); ++index) {
      result.arguments[candidate->parameters[index]].push_back(supplied[index]);
    }

    std::vector<std::optional<Type>> argument_types;
    std::size_t known_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      auto& arguments = result.arguments[index];
      if (arguments.empty() && parameters[index].default_value) {
        const auto payload = detail::parameter_default(parameters[index]);
        auto execution = payload ? detail::exec_val(*payload, parameters[index])
                                 : std::optional<detail::ExecVal>{};
        auto value = execution ? detail::stage(compiler_, std::move(*execution))
                               : std::optional<detail::StagedVal>{};
        if (!value) {
          reject("cannot construct default argument '" +
                 parameters[index].name + "'");
          return std::nullopt;
        }
        PendingArgument argument;
        argument.value = std::move(*value);
        arguments.push_back(std::move(argument));
      }
      if (arguments.empty() && !parameters[index].variadic) {
        reject("call is missing argument '" + parameters[index].name + "'");
        return std::nullopt;
      }
      if (detail::is_value_port(parameters[index])) {
        for (const PendingArgument& argument : arguments) {
          argument_types.push_back(
              argument.value ? std::optional<Type>{argument.value->type()}
                             : argument.inferred_type);
        }
        continue;
      }
      if (arguments.size() != 1U || !arguments.front().value ||
          !arguments.front().value->known()) {
        reject("argument '" + parameters[index].name +
               "' must be one Known value");
        return std::nullopt;
      }
      const detail::ExecVal* known = arguments.front().value->known_value();
      const auto payload =
          known ? detail::parameter_value(*known) : std::optional<ParamVal>{};
      if (!payload || !detail::matches_parameter(parameters[index], *payload)) {
        reject("argument '" + parameters[index].name +
               "' has an incompatible compiler domain");
        return std::nullopt;
      }
      result.known_arguments[known_index++] = *payload;
    }

    Diag attempt;
    auto types = detail::resolve_partial_call_types(
        compiler_, fn, argument_types, result.known_arguments, expected,
        errors ? *errors : attempt, source(range), allow_guarded_evaluation);
    if (!types || types->arguments.size() != argument_types.size()) {
      return std::nullopt;
    }
    std::size_t value_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (!detail::is_value_port(parameters[index])) {
        continue;
      }
      for (const PendingArgument& argument : result.arguments[index]) {
        if (argument.is_inline_fn()) {
          const Type& callable = types->arguments[value_index];
          const Mod::Symbol schema = callable.schema().symbol();
          const auto inputs = callable.get<std::vector<Type>>("inputs");
          const auto results = callable.get<std::vector<Type>>("results");
          if (schema.mod_name() != detail::prelude_mod_name ||
              schema.local_name() != "callable" || !inputs || !results ||
              results->size() != 1U || !argument.inline_inputs ||
              *inputs != *argument.inline_inputs) {
            reject("inline fn does not match argument '" +
                   parameters[index].name + "'");
            return std::nullopt;
          }
        }
        ++value_index;
      }
    }
    result.partial_types = std::move(*types);
    return result;
  }

  bool prepare_inline_fn(PendingArgument& argument, const Mod::Expr& expression,
                         detail::SyntaxRange range,
                         bool infer_signature = false) {
    if (expression.kind != Mod::Expr::Kind::Lambda) {
      return false;
    }
    argument.inline_fn = expression;
    const std::size_t parameter_count = expression.labels.size();
    const bool inferred = expression.arguments.size() == parameter_count + 1U;
    const bool annotated = expression.arguments.size() == parameter_count + 2U;
    if (!inferred && !annotated) {
      return false;
    }

    std::vector<Type> inputs;
    inputs.reserve(parameter_count);
    for (std::size_t index = 0; index < parameter_count; ++index) {
      auto input = infer_signature
                       ? infer_type({expression.arguments[index], range})
                       : type({expression.arguments[index], range});
      if (!input) {
        return false;
      }
      inputs.push_back(std::move(*input));
    }
    if (!annotated || !infer_signature) {
      argument.inline_inputs = std::move(inputs);
      return true;
    }

    auto result = infer_type({expression.arguments[parameter_count], range});
    const auto prelude = compiler_.mod(detail::prelude_mod_name);
    const auto callable =
        prelude ? prelude->type("callable") : std::optional<Mod::TypeDecl>{};
    if (!result || !callable) {
      return false;
    }
    argument.inline_inputs = inputs;
    argument.inferred_type = compiler_.make(
        *callable, std::move(inputs), std::vector<Type>{std::move(*result)});
    return argument.inferred_type.has_value();
  }

  std::optional<Type> infer_expression_type(const Mod::Expr& expression,
                                            detail::SyntaxRange range) {
    // A nested residual call may contribute a result Type to its enclosing
    // generic call before either Call is emitted. Ambiguous or contextual
    // expressions deliberately return no Type and use the existing
    // top-down path instead.
    using Kind = Mod::Expr::Kind;
    if (expression.kind == Kind::Lambda) {
      PendingArgument lambda;
      return prepare_inline_fn(lambda, expression, range, true)
                 ? lambda.inferred_type
                 : std::nullopt;
    }
    if ((expression.kind == Kind::Reference ||
         expression.kind == Kind::Variable) &&
        expression.arguments.empty()) {
      const auto value = lookup_staged(expression.text);
      return value ? std::optional<Type>{value->type()} : std::nullopt;
    }
    if (known_result(expression, range)) {
      auto value = infer_known({expression, range});
      return value ? std::optional<Type>{value->type()} : std::nullopt;
    }

    std::optional<Mod::FnDecl::Fixity> fixity;
    if (expression.kind == Kind::Prefix) {
      fixity = Mod::FnDecl::Fixity::Prefix;
    } else if (expression.kind == Kind::Infix) {
      fixity = Mod::FnDecl::Fixity::Infix;
    } else if (expression.kind == Kind::Postfix) {
      fixity = Mod::FnDecl::Fixity::Postfix;
    } else if (expression.kind != Kind::Call) {
      return std::nullopt;
    }

    std::vector<PendingArgument> arguments;
    arguments.reserve(expression.arguments.size());
    for (const Mod::Expr& child : expression.arguments) {
      PendingArgument argument;
      argument.order = arguments.size();
      if (child.kind == Kind::Lambda) {
        if (!prepare_inline_fn(argument, child, range, true)) {
          return std::nullopt;
        }
      } else if ((child.kind == Kind::Reference ||
                  child.kind == Kind::Variable) &&
                 child.arguments.empty()) {
        argument.value = lookup_staged(child.text);
        if (!argument.value) {
          return std::nullopt;
        }
      } else if (known_result(child, range)) {
        argument.value = infer_known({child, range});
        if (!argument.value) {
          return std::nullopt;
        }
      } else {
        argument.expression = child;
        argument.inferred_type = infer_expression_type(child, range);
      }
      if (!argument.valid()) {
        return std::nullopt;
      }
      arguments.push_back(std::move(argument));
    }

    const auto declarations =
        fixity ? detail::visible_operators(compiler_, owner_, expression.text,
                                           *fixity)
               : visible_fns(expression.text);
    std::vector<PendingCall> plans;
    for (const auto& fn : declarations) {
      std::vector<std::optional<Type>> expected(
          detail::value_results(fn).size());
      if (auto plan = plan_call(fn, expression, arguments, expected, range,
                                residual_control_depth_ == 0U)) {
        plans.push_back(std::move(*plan));
      }
    }
    if (plans.size() != 1U ||
        plans.front().partial_types.results.size() != 1U) {
      return std::nullopt;
    }
    return plans.front().partial_types.results.front();
  }

  std::optional<detail::StagedVal>
  infer_known(const detail::ExprSyntax& syntax) {
    auto expected = known_result(syntax.value, syntax.range);
    if (!expected) {
      return std::nullopt;
    }
    Diag attempt;
    // Type probing must not invoke an observable host action. Hermetic native
    // bindings and source-only compiler computation are safe to repeat;
    // Guarded native work must be evaluated once by an explicit @ binding.
    auto payload = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, *expected, locals_.known_bindings(),
        attempt, source(syntax.range), false);
    auto value = payload ? detail::exec_val(*payload, *expected)
                         : std::optional<detail::ExecVal>{};
    return value ? detail::stage(compiler_, std::move(*value))
                 : std::optional<detail::StagedVal>{};
  }

  std::optional<Type> infer_type(const detail::ExprSyntax& syntax) {
    const Mod::ParamDecl expected{
        "type", detail::domain_expression(detail::ValKind::Type), false,
        std::nullopt};
    Diag attempt;
    auto value = detail::evaluate_known_expression(
        compiler_, owner_, syntax.value, expected, locals_.known_bindings(),
        attempt, source(syntax.range), false);
    const Type* resolved = value ? value->as_type() : nullptr;
    return resolved ? std::optional<Type>{*resolved} : std::nullopt;
  }

  bool matches_fn_value(const Mod::FnDecl& fn, const Type& callable,
                        detail::SyntaxRange range) {
    const Mod::Symbol schema = callable.schema().symbol();
    const auto inputs = callable.get<std::vector<Type>>("inputs");
    const auto results = callable.get<std::vector<Type>>("results");
    if (schema.mod_name() != detail::prelude_mod_name ||
        schema.local_name() != "callable" || !inputs || !results ||
        !detail::compiler_inputs(fn).empty() ||
        !detail::compiler_results(fn).empty()) {
      return false;
    }
    std::vector<std::optional<Type>> expected;
    expected.reserve(results->size());
    for (const Type& result : *results) {
      expected.emplace_back(result);
    }
    Diag attempt;
    const auto resolved = detail::resolve_call_types(
        compiler_, fn, *inputs, {}, expected, attempt, source(range));
    return resolved && resolved->results == *results;
  }

  std::optional<Val>
  fn_reference(std::string_view reference, detail::SyntaxRange range,
               std::optional<Type> expected_type = std::nullopt) {
    const auto overloads = visible_fns(reference);
    if (overloads.empty()) {
      return std::nullopt;
    }
    if (expected_type) {
      std::vector<Mod::FnDecl> matches;
      for (const auto& overload : overloads) {
        if (matches_fn_value(overload, *expected_type, range)) {
          matches.push_back(overload);
        }
      }
      if (matches.empty()) {
        report("no overload of fn '" + std::string(reference) +
                   "' matches the contextual callable type",
               range);
        return std::nullopt;
      }
      if (matches.size() != 1U) {
        report("fn value '" + std::string(reference) +
                   "' remains ambiguous for the contextual callable type",
               range);
        return std::nullopt;
      }
      return edit_->reference(matches.front(), std::move(*expected_type));
    }
    if (overloads.size() != 1U) {
      report("overloaded fn '" + std::string(reference) +
                 "' needs a contextual callable type",
             range);
      return std::nullopt;
    }
    const Mod::FnDecl declaration = overloads.front();
    if (!declaration.generics().empty()) {
      report("generic fn '" + std::string(reference) +
                 "' needs a contextual callable type",
             range);
      return std::nullopt;
    }
    if (!detail::compiler_inputs(declaration).empty() ||
        !detail::compiler_results(declaration).empty()) {
      report("fn value '" + std::string(reference) +
                 "' requires compile-time specialization",
             range);
      return std::nullopt;
    }

    const Mod::ParamDecl expected{
        "fn value type", detail::domain_expression(detail::ValKind::Type),
        false, std::nullopt};
    const detail::KnownBindings bindings;
    const auto resolve_ports =
        [&](const auto& ports) -> std::optional<std::vector<Type>> {
      std::vector<Type> types;
      types.reserve(ports.size());
      for (const auto& port : ports) {
        auto value = detail::evaluate_known_expression(
            compiler_, declaration.symbol().mod_name(), port.domain, expected,
            bindings, diagnostics_, source(range),
            residual_control_depth_ == 0U);
        const Type* type = value ? value->as_type() : nullptr;
        if (type == nullptr) {
          return std::nullopt;
        }
        types.push_back(*type);
      }
      return types;
    };
    auto inputs = resolve_ports(detail::value_inputs(declaration));
    auto results = resolve_ports(detail::value_results(declaration));
    const auto prelude = compiler_.mod(detail::prelude_mod_name);
    const auto callable =
        prelude ? prelude->type("callable") : std::optional<Mod::TypeDecl>{};
    auto type = inputs && results && callable
                    ? compiler_.make(*callable, *inputs, *results)
                    : std::optional<Type>{};
    if (!type) {
      report("cannot construct callable type for fn '" +
                 std::string(reference) + "'",
             range);
      return std::nullopt;
    }
    return edit_->reference(declaration, std::move(*type));
  }

  std::optional<Fn>
  build_inline_fn(const Mod::Expr& expression,
                  const std::vector<Type>* expected_inputs,
                  std::optional<std::vector<Type>> expected_results,
                  detail::SyntaxRange range,
                  std::vector<std::pair<std::string, Type>> captures = {}) {
    return detail::instantiate_lambda(
        compiler_, owner_, expression, source(range), diagnostics_,
        locals_.known_bindings(),
        expected_inputs ? std::optional<std::vector<Type>>{*expected_inputs}
                        : std::nullopt,
        std::move(expected_results), residual_control_depth_ == 0U,
        std::move(captures));
  }

  void collect_free_variables(const Mod::Expr& expression,
                              std::vector<std::string>& bound,
                              std::vector<std::string>& free) const {
    using Kind = Mod::Expr::Kind;
    const auto is_bound = [&](std::string_view name) {
      return std::find(bound.begin(), bound.end(), name) != bound.end();
    };
    const auto remember = [&](std::string_view name) {
      if (!name.empty() && !is_bound(name) && declared_local(name) &&
          std::find(free.begin(), free.end(), name) == free.end()) {
        free.emplace_back(name);
      }
    };

    if (expression.kind == Kind::Variable) {
      remember(expression.text);
      return;
    }
    if (expression.kind == Kind::Call &&
        expression.text.find('.') == std::string::npos) {
      remember(expression.text);
    }
    if (expression.kind == Kind::Lambda) {
      const std::size_t previous = bound.size();
      bound.insert(bound.end(), expression.labels.begin(),
                   expression.labels.end());
      if (!expression.arguments.empty()) {
        collect_free_variables(expression.arguments.back(), bound, free);
      }
      bound.resize(previous);
      return;
    }
    for (const Mod::Expr& argument : expression.arguments) {
      collect_free_variables(argument, bound, free);
    }
  }

  std::optional<std::vector<std::pair<std::string, Val>>>
  closure_captures(const Mod::Expr& expression,
                   detail::SyntaxRange range) const {
    std::vector<std::string> bound = expression.labels;
    std::vector<std::string> free;
    if (!expression.arguments.empty()) {
      collect_free_variables(expression.arguments.back(), bound, free);
    }
    std::vector<std::pair<std::string, Val>> captures;
    for (const std::string& name : free) {
      const detail::StagedVal* value = locals_.find(name);
      if (value == nullptr || value->known()) {
        continue;
      }
      const Val* residual = value->residual_value();
      if (residual == nullptr) {
        continue;
      }
      if (detail::is_effect_type(residual->type())) {
        diagnostics_.report("inline fn cannot implicitly capture effect '" +
                                name + "'",
                            source(range));
        return std::nullopt;
      }
      captures.emplace_back(name, *residual);
    }
    return captures;
  }

  std::optional<Val> inline_fn(const Mod::Expr& expression,
                               const Type& callable,
                               detail::SyntaxRange range) {
    const Mod::Symbol schema = callable.schema().symbol();
    const auto inputs = callable.get<std::vector<Type>>("inputs");
    const auto results = callable.get<std::vector<Type>>("results");
    if (schema.mod_name() != detail::prelude_mod_name ||
        schema.local_name() != "callable" || !inputs || !results ||
        results->size() != 1U) {
      report("inline fn does not match its callable context", range);
      return std::nullopt;
    }
    auto captures = closure_captures(expression, range);
    if (!captures) {
      return std::nullopt;
    }
    std::vector<std::pair<std::string, Type>> capture_types;
    std::vector<Val> capture_values;
    capture_types.reserve(captures->size());
    capture_values.reserve(captures->size());
    for (const auto& [name, value] : *captures) {
      capture_types.emplace_back(name, value.type());
      capture_values.push_back(value);
    }
    auto fn = build_inline_fn(expression, &*inputs, *results, range,
                              std::move(capture_types));
    if (!fn) {
      return std::nullopt;
    }
    return edit_->callable(std::move(*fn), callable, std::move(capture_values));
  }

  std::optional<detail::ExecVal> compiler_fn(const Mod::Expr& expression,
                                             detail::SyntaxRange range) {
    auto fn = build_inline_fn(expression, nullptr, std::nullopt, range);
    return fn ? std::optional<detail::ExecVal>{detail::store_exec_val(
                    std::move(*fn))}
              : std::nullopt;
  }

  std::optional<Val> use(const detail::LocalUseSyntax& use) {
    if (auto value = lookup(use.name)) {
      return value;
    }
    report("use of undefined local value '" + use.name + "'", use.range);
    return std::nullopt;
  }

  std::optional<Val> use(const detail::ExprSyntax& expression,
                         std::optional<Type> expected_type = std::nullopt) {
    if ((expression.value.kind != Mod::Expr::Kind::Reference &&
         expression.value.kind != Mod::Expr::Kind::Variable) ||
        !expression.value.arguments.empty()) {
      report("expected a local value reference", expression.range);
      return std::nullopt;
    }
    if (auto value = lookup(expression.value.text)) {
      return value;
    }
    if (declared_local(expression.value.text)) {
      return std::nullopt;
    }
    if (expression.value.kind == Mod::Expr::Kind::Reference) {
      if (auto value = fn_reference(expression.value.text, expression.range,
                                    std::move(expected_type))) {
        return value;
      }
      if (!visible_fns(expression.value.text).empty()) {
        return std::nullopt;
      }
    }
    report("use of undefined local value '" + expression.value.text + "'",
           expression.range);
    return std::nullopt;
  }

  void define(std::string name, Val value, detail::SyntaxRange range) {
    auto staged = detail::stage(std::move(value));
    if (!staged) {
      report("a local value cannot enter staged evaluation", range);
      return;
    }
    define_staged(std::move(name), std::move(*staged), range);
  }

  void define_staged(std::string name, detail::StagedVal value,
                     detail::SyntaxRange range) {
    if (!locals_.define(std::move(name), std::move(value))) {
      report("a local value may only be defined once", range);
    }
  }

  void bind(const detail::BindingSyntax& binding, std::optional<Val> value) {
    std::optional<detail::StagedVal> staged;
    if (value) {
      staged = detail::stage(std::move(*value));
      if (!staged) {
        report("a local value cannot enter staged evaluation", binding.range);
        return;
      }
    }
    bind_staged(binding, std::move(staged));
  }

  void bind_staged(const detail::BindingSyntax& binding,
                   std::optional<detail::StagedVal> value) {
    if (!binding.rebind) {
      if (!locals_.define(binding.name, std::move(value))) {
        report("a local value may only be defined once", binding.range);
      }
      return;
    }
    if (locals_.assign(binding.name, std::move(value))) {
      return;
    }
    report("cannot rebind undefined local value '" + binding.name + "'",
           binding.range);
  }

  void invalidate(std::span<const detail::BindingSyntax> bindings) {
    for (const auto& binding : bindings) {
      bind(binding, std::nullopt);
    }
  }

  std::optional<Type> expected_type(const detail::BindingSyntax& binding) {
    if (binding.type) {
      return type(*binding.type);
    }
    const auto expected = expected_values_.find(binding.name);
    return expected == expected_values_.end()
               ? std::optional<Type>{}
               : std::optional<Type>{expected->second};
  }

  std::optional<Val> materialize(Val value, Type target, Blk block,
                                 detail::SyntaxRange range) {
    if (!value.known()) {
      if (value.type() != target) {
        report("Residual value has the wrong materialized type", range);
        return std::nullopt;
      }
      return value;
    }
    const auto payload = detail::FnAccess::known_value(value);
    if (!payload) {
      report("no literal fn is available for this Known value", range);
      return std::nullopt;
    }

    std::vector<Mod> visible;
    std::unordered_set<std::string> seen_mods;
    const auto add_mod = [&](std::string_view name) {
      if (!seen_mods.insert(std::string(name)).second) {
        return;
      }
      if (auto mod = compiler_.mod(name)) {
        visible.push_back(std::move(*mod));
      }
    };
    add_mod(owner_);
    const auto owner = compiler_.mod(owner_);
    if (owner) {
      for (const auto& import : owner->imports()) {
        add_mod(import.name);
      }
    }
    add_mod(detail::prelude_mod_name);

    std::vector<Mod::FnDecl> matches;
    for (const Mod& mod : visible) {
      for (const auto& candidate : mod.fns()) {
        if (candidate.name() != "literal" ||
            detail::compiler_inputs(candidate).size() != 1U ||
            !detail::value_inputs(candidate).empty() ||
            !detail::compiler_results(candidate).empty() ||
            detail::value_results(candidate).size() != 1U) {
          continue;
        }
        Diag candidate_diagnostics;
        const std::array<std::optional<ParamVal>, 1> known{payload};
        const std::array<std::optional<Type>, 1> expected{target};
        if (detail::resolve_call_types(compiler_, candidate, {}, known,
                                       expected, candidate_diagnostics)) {
          matches.push_back(candidate);
        }
      }
    }
    if (matches.empty()) {
      report("no visible literal fn can materialize Known value as '" +
                 std::string(target.schema().name()) + "'",
             range);
      return std::nullopt;
    }
    if (matches.size() != 1U) {
      std::string message =
          "more than one visible literal fn can materialize this value:";
      for (const auto& candidate : matches) {
        message += " '" + candidate.symbol().qualified_name() + "'";
      }
      report(std::move(message), range);
      return std::nullopt;
    }

    Op op = edit_->call(block, matches.front(), {value}, {target});
    detail::FnAccess::locate(*edit_, op, source(range));
    return op.result(0);
  }

  std::pair<Blk, std::optional<Val>>
  instantiate_expression(const Mod::Expr& expression, detail::SyntaxRange range,
                         Blk block,
                         std::optional<Type> expected = std::nullopt) {
    using Kind = Mod::Expr::Kind;
    const detail::ExprSyntax syntax{expression, range};
    if ((expression.kind == Kind::Variable ||
         expression.kind == Kind::Reference) &&
        expression.arguments.empty()) {
      const auto found = expected_values_.find(expression.text);
      return {block, use(syntax, found == expected_values_.end()
                                     ? std::optional<Type>{}
                                     : std::optional<Type>{found->second})};
    }
    const std::string name = "$value" + std::to_string(next_temporary_++);
    if (expected) {
      expected_values_.insert_or_assign(name, *expected);
    }
    detail::StatementSyntax statement;
    statement.bindings.push_back({name, std::nullopt, range});
    statement.expression = syntax;
    statement.range = range;
    Flow flow = instantiate_statement(statement, block);
    if (expected) {
      expected_values_.erase(name);
    }
    if (flow.next.size() != 1U || !flow.breaks.empty() ||
        !flow.continues.empty()) {
      report("expression produced non-local control flow", range);
      return {block, std::nullopt};
    }
    restore(flow.next.front());
    return {flow.next.front().block, use(detail::LocalUseSyntax{name, range})};
  }

  void
  collect_rebindings(const std::vector<detail::StatementSyntax>& statements,
                     std::vector<std::string>& names,
                     std::unordered_set<std::string>& seen) const {
    for (const auto& statement : statements) {
      if (statement.kind != detail::StatementSyntax::Kind::Expr) {
        collect_rebindings(statement.body, names, seen);
        if (statement.kind == detail::StatementSyntax::Kind::If) {
          collect_rebindings(statement.otherwise, names, seen);
        }
        continue;
      }
      for (const auto& binding : statement.bindings) {
        if (binding.rebind && lookup(binding.name) &&
            seen.insert(binding.name).second) {
          names.push_back(binding.name);
        }
      }
    }
  }

  void collect_effect_state(std::vector<std::string>& names,
                            std::unordered_set<std::string>& seen) const {
    for (const std::string& name : locals_.names()) {
      const auto value = lookup_staged(name);
      if (value && !value->known() && detail::is_effect_type(value->type()) &&
          seen.insert(name).second) {
        names.push_back(name);
      }
    }
  }

  Flow instantiate_if_statement(const detail::StatementSyntax& statement,
                                Blk block) {
    if (known_result(statement.expression.value, statement.expression.range)) {
      auto condition = evaluate_known(statement.expression);
      const auto selected =
          condition ? detail::known_boolean(*condition) : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               statement.expression.range);
        return next(block);
      }
      const auto& arm = *selected ? statement.body : statement.otherwise;
      const std::size_t outer_depth = locals_.depth();
      locals_.push();
      Flow flow = instantiate_sequence(arm, block);
      trim_scopes(flow, outer_depth);
      return flow;
    }

    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);
    collect_rebindings(statement.otherwise, carried_names, seen);
    collect_effect_state(carried_names, seen);

    auto [condition_tail, condition] = instantiate_expression(
        statement.expression.value, statement.expression.range, block);
    const auto i1 = compiler_.make("i1");
    if (!condition || condition->known() || !i1 || condition->type() != *i1) {
      report("Residual if condition must have type i1",
             statement.expression.range);
      return next(block);
    }
    std::vector<std::size_t> effect_carried;
    std::vector<Type> effect_types;
    std::vector<Val> effect_values;
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      const auto value = lookup_staged(carried_names[index]);
      if (!value || value->known() || !detail::is_effect_type(value->type())) {
        continue;
      }
      const auto residual = value->residual_value();
      if (residual == nullptr) {
        continue;
      }
      effect_carried.push_back(index);
      effect_types.push_back(value->type());
      effect_values.push_back(*residual);
    }

    const Blk yes = edit_->blk(effect_types);
    const Blk no = edit_->blk(effect_types);
    edit_->branch(condition_tail, *condition, yes, effect_values, no,
                  effect_values);

    const detail::Locals incoming = locals_;
    const auto elaborate = [&](const std::vector<detail::StatementSyntax>& arm,
                               Blk start) {
      locals_ = incoming;
      locals_.push();
      const auto arguments = start.arguments();
      for (std::size_t index = 0; index < effect_carried.size(); ++index) {
        define(carried_names[effect_carried[index]], arguments[index],
               statement.range);
      }
      Flow flow = instantiate_sequence(arm, start);
      std::vector<std::optional<detail::StagedVal>> values;
      values.reserve(carried_names.size());
      if (flow.next.size() == 1U) {
        restore(flow.next.front());
        for (const std::string& name : carried_names) {
          values.push_back(lookup_staged(name));
        }
      }
      trim_scopes(flow, incoming.depth());
      return std::pair{flow, std::move(values)};
    };

    ++residual_control_depth_;
    auto [true_flow, true_values] = elaborate(statement.body, yes);
    auto [false_flow, false_values] = elaborate(statement.otherwise, no);
    --residual_control_depth_;
    locals_ = incoming;

    const bool transfers =
        !true_flow.breaks.empty() || !true_flow.continues.empty() ||
        !false_flow.breaks.empty() || !false_flow.continues.empty();
    if (transfers) {
      Flow result;
      result.append(std::move(true_flow));
      result.append(std::move(false_flow));
      return result;
    }
    const bool true_continues = true_flow.next.size() == 1U;
    const bool false_continues = false_flow.next.size() == 1U;
    if (!true_continues && !false_continues) {
      return {};
    }
    if (!true_continues || !false_continues) {
      const auto& values = true_continues ? true_values : false_values;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        if (!values[index]) {
          report("surviving if arm does not produce outer binding '" +
                     carried_names[index] + "'",
                 statement.range);
          continue;
        }
        bind_staged({carried_names[index], std::nullopt, statement.range, true},
                    values[index]);
      }
      return true_continues ? std::move(true_flow) : std::move(false_flow);
    }

    std::vector<Type> merge_types;
    std::vector<Val> true_arguments;
    std::vector<Val> false_arguments;
    std::vector<std::optional<detail::StagedVal>> unchanged(
        carried_names.size());
    std::vector<std::optional<std::size_t>> merged(carried_names.size());
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      if (!true_values[index] || !false_values[index]) {
        report("if arm does not produce outer binding '" +
                   carried_names[index] + "'",
               statement.range);
        continue;
      }
      if (detail::same_staged_value(*true_values[index],
                                    *false_values[index])) {
        unchanged[index] = *true_values[index];
        continue;
      }
      std::optional<Type> target;
      if (!true_values[index]->known()) {
        target = true_values[index]->type();
      } else if (!false_values[index]->known()) {
        target = false_values[index]->type();
      } else if (true_values[index]->type() == false_values[index]->type()) {
        target = true_values[index]->type();
      }
      if (!target ||
          (!true_values[index]->known() &&
           true_values[index]->type() != *target) ||
          (!false_values[index]->known() &&
           false_values[index]->type() != *target)) {
        report("if arms assign incompatible types to '" + carried_names[index] +
                   "'",
               statement.range);
        continue;
      }
      auto true_ir = detail::ir_value(compiler_, *true_values[index]);
      auto false_ir = detail::ir_value(compiler_, *false_values[index]);
      auto true_value =
          true_ir ? materialize(*true_ir, *target, true_flow.next.front().block,
                                statement.range)
                  : std::optional<Val>{};
      auto false_value =
          false_ir ? materialize(*false_ir, *target,
                                 false_flow.next.front().block, statement.range)
                   : std::optional<Val>{};
      if (!true_value || !false_value) {
        continue;
      }
      merged[index] = merge_types.size();
      merge_types.push_back(*target);
      true_arguments.push_back(*true_value);
      false_arguments.push_back(*false_value);
    }
    if (!ok()) {
      return next(block);
    }

    const Blk merge = edit_->blk(merge_types);
    edit_->jump(true_flow.next.front().block, merge, true_arguments);
    edit_->jump(false_flow.next.front().block, merge, false_arguments);
    const auto arguments = merge.arguments();
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      const detail::BindingSyntax binding{carried_names[index], std::nullopt,
                                          statement.range, true};
      if (merged[index]) {
        bind(binding, arguments[*merged[index]]);
      } else {
        bind_staged(binding, unchanged[index]);
      }
    }
    return next(merge);
  }

  Flow instantiate_while(const detail::StatementSyntax& statement, Blk block) {
    std::vector<std::string> carried_names;
    std::unordered_set<std::string> seen;
    collect_rebindings(statement.body, carried_names, seen);
    collect_effect_state(carried_names, seen);

    if (known_result(statement.expression.value, statement.expression.range)) {
      struct StagedState {
        std::vector<detail::StagedVal> values;
        Blk block;
      };
      std::vector<StagedState> visited;
      std::vector<Path> pending{path(block)};
      Flow exits;
      while (!pending.empty() && ok()) {
        Path active = std::move(pending.back());
        pending.pop_back();
        restore(active);
        auto condition = evaluate_known(statement.expression);
        const auto selected =
            condition ? detail::known_boolean(*condition) : std::nullopt;
        if (!selected) {
          report("Known while condition must have type bool",
                 statement.expression.range);
          continue;
        }
        if (!*selected) {
          exits.next.push_back(path(active.block));
          continue;
        }
        std::vector<detail::StagedVal> state;
        state.reserve(carried_names.size());
        for (const std::string& name : carried_names) {
          if (auto value = lookup_staged(name)) {
            state.push_back(std::move(*value));
          }
        }
        const auto repeated =
            state.size() == carried_names.size()
                ? std::find_if(visited.begin(), visited.end(),
                               [&](const StagedState& previous) {
                                 return same_staged_state(previous.values,
                                                          state);
                               })
                : visited.end();
        if (repeated != visited.end() && active.residual_depth != 0U) {
          edit_->jump(active.block, repeated->block);
          continue;
        }
        if (state.size() == carried_names.size()) {
          visited.push_back({std::move(state), active.block});
        }
        if (loop_iterations_++ >= compiler_.evaluation_limits().steps) {
          report(active.residual_depth == 0U
                     ? "compile-time while iteration limit exceeded"
                     : "mixed-stage while does not reach a finite "
                       "specialization; make its condition and carried "
                       "state Residual",
                 statement.range);
          continue;
        }
        const std::size_t outer_depth = locals_.depth();
        locals_.push();
        loops_.push_back({});
        Flow flow = instantiate_sequence(statement.body, active.block);
        loops_.pop_back();
        trim_scopes(flow, outer_depth);
        exits.next.insert(exits.next.end(),
                          std::make_move_iterator(flow.breaks.begin()),
                          std::make_move_iterator(flow.breaks.end()));
        pending.insert(pending.end(),
                       std::make_move_iterator(flow.next.begin()),
                       std::make_move_iterator(flow.next.end()));
        pending.insert(pending.end(),
                       std::make_move_iterator(flow.continues.begin()),
                       std::make_move_iterator(flow.continues.end()));
      }
      return exits;
    }

    std::vector<Val> initial;
    std::vector<Type> carried_types;
    for (const std::string& name : carried_names) {
      auto value = lookup(name);
      if (!value) {
        report("loop-carried value '" + name + "' is unavailable",
               statement.range);
        continue;
      }
      std::optional<Type> target;
      if (!value->known()) {
        target = value->type();
      } else if (const auto expected = expected_values_.find(name);
                 expected != expected_values_.end()) {
        target = expected->second;
      }
      if (!target) {
        report("Known loop-carried value '" + name +
                   "' needs an explicit or downstream mod type",
               statement.range);
        continue;
      }
      carried_types.push_back(*target);
      auto carried = materialize(*value, *target, block, statement.range);
      if (carried) {
        initial.push_back(*carried);
      }
    }
    if (initial.size() != carried_names.size()) {
      return next(block);
    }

    std::vector<std::size_t> effect_carried;
    std::vector<Type> effect_types;
    for (std::size_t index = 0; index < carried_types.size(); ++index) {
      if (detail::is_effect_type(carried_types[index])) {
        effect_carried.push_back(index);
        effect_types.push_back(carried_types[index]);
      }
    }

    const Blk header = edit_->blk(carried_types);
    const Blk body = edit_->blk(effect_types);
    const Blk exit = edit_->blk(carried_types);
    edit_->jump(block, header, initial);

    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      bind({carried_names[index], std::nullopt, statement.range, true},
           header.arguments()[index]);
    }
    auto [condition_tail, condition] = instantiate_expression(
        statement.expression.value, statement.expression.range, header);
    const auto i1 = compiler_.make("i1");
    if (!condition || condition->known() || !i1 || condition->type() != *i1) {
      report("Residual while condition must have type i1",
             statement.expression.range);
      return next(block);
    }
    std::vector<Val> body_arguments;
    body_arguments.reserve(effect_carried.size());
    for (const std::size_t index : effect_carried) {
      body_arguments.push_back(header.arguments()[index]);
    }
    edit_->branch(condition_tail, *condition, body, body_arguments, exit,
                  header.arguments());

    const std::size_t outer_scope_depth = locals_.depth();
    const std::size_t outer_residual_depth = residual_control_depth_;
    ++residual_control_depth_;
    locals_.push();
    for (std::size_t index = 0; index < effect_carried.size(); ++index) {
      define(carried_names[effect_carried[index]], body.arguments()[index],
             statement.range);
    }
    loops_.push_back({header, exit, carried_names, carried_types});
    Flow body_flow = instantiate_sequence(statement.body, body);
    loops_.pop_back();
    for (const Path& active : body_flow.next) {
      restore(active);
      std::vector<Val> updated;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        const std::string& name = carried_names[index];
        if (auto value = lookup(name)) {
          auto carried = materialize(*value, carried_types[index], active.block,
                                     statement.range);
          if (carried) {
            updated.push_back(*carried);
          }
        }
      }
      if (updated.size() != carried_names.size()) {
        report("while body does not produce every loop-carried value",
               statement.range);
        continue;
      }
      edit_->jump(active.block, header, std::move(updated));
    }
    if (!body_flow.breaks.empty() || !body_flow.continues.empty()) {
      report("loop control was not consumed by its Residual loop",
             statement.range);
    }
    locals_.resize(outer_scope_depth);
    residual_control_depth_ = outer_residual_depth;
    for (std::size_t index = 0; index < carried_names.size(); ++index) {
      bind({carried_names[index], std::nullopt, statement.range, true},
           exit.arguments()[index]);
    }
    return next(exit);
  }

  struct CountedRange {
    std::int64_t start = 0;
    std::int64_t limit = 0;
    std::int64_t step = 1;
    bool empty = false;
  };

  struct CountedRangeProbe {
    bool matched = false;
    std::optional<CountedRange> value;
  };

  CountedRangeProbe prelude_counted_range(const detail::ExprSyntax& syntax) {
    using Kind = Mod::Expr::Kind;
    const Mod::Expr* expression = &syntax.value;
    if (expression->kind == Kind::Evaluate &&
        expression->arguments.size() == 1U) {
      expression = &expression->arguments.front();
    }
    if (expression->kind != Kind::Call) {
      return {};
    }

    std::optional<Mod::FnDecl> selected;
    std::optional<detail::CallCandidate> selected_call;
    for (const auto& fn : visible_fns(expression->text)) {
      const auto candidate = detail::call_candidate(fn, *expression);
      if (!candidate || !detail::is_prelude_primitive(fn) ||
          fn.name() != "range") {
        continue;
      }
      if (selected) {
        return {};
      }
      selected = fn;
      selected_call = *candidate;
    }
    if (!selected || !selected_call) {
      return {};
    }

    std::array<std::int64_t, 3> arguments{0, 0, 1};
    for (std::size_t index = 0; index < expression->arguments.size(); ++index) {
      const std::size_t parameter = selected_call->parameters[index];
      auto value = evaluate_known({expression->arguments[index], syntax.range},
                                  selected->inputs()[parameter]);
      const auto* integer =
          value && value->known()
              ? std::get_if<std::int64_t>(value->known_value())
              : nullptr;
      if (integer == nullptr) {
        return {true, std::nullopt};
      }
      arguments[parameter] = *integer;
    }

    const std::size_t count = expression->arguments.size();
    const std::int64_t start = count == 1U ? 0 : arguments[0];
    const std::int64_t stop = count == 1U ? arguments[0] : arguments[1];
    const std::int64_t step = count == 3U ? arguments[2] : 1;
    if (step == 0) {
      report("range step cannot be zero", syntax.range);
      return {true, std::nullopt};
    }
    if (step > 0 ? start >= stop : start <= stop) {
      return {true, CountedRange{start, stop, step, true}};
    }

    const auto checked_add =
        [](std::int64_t lhs, std::int64_t rhs) -> std::optional<std::int64_t> {
      if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
          (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
        return std::nullopt;
      }
      return lhs + rhs;
    };
    std::int64_t last = start;
    if (step > 0) {
      const std::uint64_t distance =
          static_cast<std::uint64_t>(stop) - static_cast<std::uint64_t>(start);
      const std::uint64_t remainder =
          (distance - 1U) % static_cast<std::uint64_t>(step);
      last = (stop - 1) - static_cast<std::int64_t>(remainder);
    } else {
      const std::uint64_t distance =
          static_cast<std::uint64_t>(start) - static_cast<std::uint64_t>(stop);
      const std::uint64_t magnitude =
          std::uint64_t{0} - static_cast<std::uint64_t>(step);
      const std::uint64_t remainder = (distance - 1U) % magnitude;
      last = (stop + 1) + static_cast<std::int64_t>(remainder);
    }
    const auto limit = checked_add(last, step);
    if (!limit) {
      report("typed for range exceeds the compiler integer domain",
             syntax.range);
      return {true, std::nullopt};
    }
    return {true, CountedRange{start, *limit, step, false}};
  }

  Flow instantiate_for(const detail::StatementSyntax& statement, Blk block) {
    if (!statement.iterator) {
      report("for statement has no iterator", statement.range);
      return next(block);
    }
    std::optional<CountedRange> counted;
    std::optional<std::vector<detail::ExecVal>> elements;
    if (statement.iterator->type) {
      auto probe = prelude_counted_range(statement.expression);
      if (probe.matched) {
        if (!probe.value) {
          return next(block);
        }
        counted = *probe.value;
      }
    }
    if (!counted) {
      auto iterable = evaluate_known(statement.expression);
      const detail::ExecVal* payload =
          iterable && iterable->known() ? iterable->known_value() : nullptr;
      elements = payload ? detail::list_elements(*payload)
                         : std::optional<std::vector<detail::ExecVal>>{};
      if (!elements) {
        report("for iterable must be a Known list", statement.expression.range);
        return next(block);
      }
    }

    if (statement.iterator->type) {
      const auto iterator_type = expected_type(*statement.iterator);
      if (!iterator_type) {
        return next(block);
      }
      if ((counted && counted->empty) || (elements && elements->empty())) {
        return next(block);
      }

      std::vector<std::int64_t> sequence;
      if (elements) {
        sequence.reserve(elements->size());
        for (const detail::ExecVal& element : *elements) {
          const auto* integer = std::get_if<std::int64_t>(&element);
          if (!integer) {
            report("a typed for iterable must contain integers",
                   statement.expression.range);
            return next(block);
          }
          sequence.push_back(*integer);
        }
      }

      const auto checked_add =
          [](std::int64_t lhs,
             std::int64_t rhs) -> std::optional<std::int64_t> {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs) ||
            (rhs < 0 && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
          return std::nullopt;
        }
        return lhs + rhs;
      };
      const auto checked_subtract =
          [](std::int64_t lhs,
             std::int64_t rhs) -> std::optional<std::int64_t> {
        if ((rhs > 0 && lhs < std::numeric_limits<std::int64_t>::min() + rhs) ||
            (rhs < 0 && lhs > std::numeric_limits<std::int64_t>::max() + rhs)) {
          return std::nullopt;
        }
        return lhs - rhs;
      };

      std::int64_t start_integer = 0;
      std::int64_t limit_integer = 0;
      std::int64_t step = 1;
      if (counted) {
        start_integer = counted->start;
        limit_integer = counted->limit;
        step = counted->step;
      } else {
        start_integer = sequence.front();
        if (sequence.size() > 1U) {
          const auto difference = checked_subtract(sequence[1], sequence[0]);
          if (!difference || *difference == 0) {
            report("a typed for iterable must be an arithmetic progression",
                   statement.expression.range);
            return next(block);
          }
          step = *difference;
          for (std::size_t index = 2; index < sequence.size(); ++index) {
            const auto next_step =
                checked_subtract(sequence[index], sequence[index - 1U]);
            if (!next_step || *next_step != step) {
              report("a typed for iterable must be an arithmetic progression",
                     statement.expression.range);
              return next(block);
            }
          }
        } else if (sequence.front() ==
                   std::numeric_limits<std::int64_t>::max()) {
          step = -1;
        }
        const auto limit = checked_add(sequence.back(), step);
        if (!limit) {
          report("typed for range exceeds the compiler integer domain",
                 statement.expression.range);
          return next(block);
        }
        limit_integer = *limit;
      }

      std::vector<std::string> carried_names;
      std::unordered_set<std::string> seen;
      collect_rebindings(statement.body, carried_names, seen);
      collect_effect_state(carried_names, seen);
      std::vector<Type> carried_types;
      std::vector<Val> initial;
      for (const std::string& name : carried_names) {
        auto value = lookup(name);
        if (!value) {
          report("loop-carried value '" + name + "' is unavailable",
                 statement.range);
          continue;
        }
        std::optional<Type> target;
        if (!value->known()) {
          target = value->type();
        } else if (const auto expected = expected_values_.find(name);
                   expected != expected_values_.end()) {
          target = expected->second;
        }
        if (!target) {
          report("Known loop-carried value '" + name +
                     "' needs an explicit or downstream mod type",
                 statement.range);
          continue;
        }
        carried_types.push_back(*target);
        auto carried = materialize(*value, *target, block, statement.range);
        if (carried) {
          initial.push_back(*carried);
        }
      }
      if (initial.size() != carried_names.size()) {
        return next(block);
      }

      const auto materialize_integer = [&](std::int64_t integer) {
        auto staged = detail::stage(compiler_, detail::ExecVal{integer});
        auto known = staged ? detail::ir_value(compiler_, *staged)
                            : std::optional<Val>{};
        return known ? materialize(*known, *iterator_type, block,
                                   statement.iterator->range)
                     : std::optional<Val>{};
      };
      auto start = materialize_integer(start_integer);
      auto bound = materialize_integer(limit_integer);
      auto step_value = materialize_integer(step);
      if (!start || !bound || !step_value) {
        report("typed for bounds cannot be materialized as its iterator type",
               statement.iterator->range);
        return next(block);
      }

      std::vector<Type> header_types{*iterator_type};
      header_types.insert(header_types.end(), carried_types.begin(),
                          carried_types.end());
      std::vector<Val> header_initial{*start};
      header_initial.insert(header_initial.end(), initial.begin(),
                            initial.end());
      std::vector<std::size_t> effect_carried;
      std::vector<Type> effect_types;
      for (std::size_t index = 0; index < carried_types.size(); ++index) {
        if (detail::is_effect_type(carried_types[index])) {
          effect_carried.push_back(index);
          effect_types.push_back(carried_types[index]);
        }
      }
      const Blk header = edit_->blk(header_types);
      const Blk body = edit_->blk(effect_types);
      const Blk exit = edit_->blk(carried_types);
      edit_->jump(block, header, std::move(header_initial));
      const std::vector<Val> header_arguments = header.arguments();

      const std::size_t outer_scope_depth = locals_.depth();
      const detail::Locals outer_locals = locals_;
      const std::size_t outer_residual_depth = residual_control_depth_;
      ++residual_control_depth_;
      locals_.push();
      const std::string state_name =
          "$for.iterator" + std::to_string(next_temporary_++);
      const std::string limit_name =
          "$for.limit" + std::to_string(next_temporary_++);
      const std::string step_name =
          "$for.step" + std::to_string(next_temporary_++);
      define(state_name, header_arguments.front(), statement.iterator->range);
      define(statement.iterator->name, header_arguments.front(),
             statement.iterator->range);
      define(limit_name, *bound, statement.expression.range);
      define(step_name, *step_value, statement.expression.range);
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        bind({carried_names[index], std::nullopt, statement.range, true},
             header_arguments[index + 1U]);
      }

      const auto infix = [](std::string symbol, std::string lhs,
                            std::string rhs) {
        return Mod::Expr{Mod::Expr::Kind::Infix,
                         std::move(symbol),
                         {Mod::Expr::reference(std::move(lhs)),
                          Mod::Expr::reference(std::move(rhs))}};
      };
      auto [condition_tail, condition] = instantiate_expression(
          infix(step > 0 ? "<" : ">", state_name, limit_name),
          statement.expression.range, header);
      const auto i1 = compiler_.make("i1");
      if (!condition || condition->known() || !i1 || condition->type() != *i1) {
        report("typed for comparison must produce i1",
               statement.expression.range);
        return next(block);
      }
      std::vector<Val> exit_values(header_arguments.begin() + 1,
                                   header_arguments.end());
      std::vector<Val> body_values;
      body_values.reserve(effect_carried.size());
      for (const std::size_t index : effect_carried) {
        body_values.push_back(header_arguments[index + 1U]);
      }
      edit_->branch(condition_tail, *condition, body, body_values, exit,
                    std::move(exit_values));

      for (std::size_t index = 0; index < effect_carried.size(); ++index) {
        define(carried_names[effect_carried[index]], body.arguments()[index],
               statement.range);
      }

      const detail::Locals loop_locals = locals_;
      loops_.push_back({});
      Flow body_flow = instantiate_sequence(statement.body, body);
      loops_.pop_back();

      const auto carried_values = [&](const Path& active) {
        std::vector<Val> values;
        restore(active);
        for (std::size_t index = 0; index < carried_names.size(); ++index) {
          if (auto value = lookup(carried_names[index])) {
            auto carried = materialize(*value, carried_types[index],
                                       active.block, statement.range);
            if (carried) {
              values.push_back(*carried);
            }
          }
        }
        if (values.size() != carried_names.size()) {
          report("for body does not produce every loop-carried value",
                 statement.range);
        }
        return values;
      };

      for (const Path& active : body_flow.breaks) {
        auto values = carried_values(active);
        if (values.size() == carried_names.size()) {
          edit_->jump(active.block, exit, std::move(values));
        }
      }
      body_flow.next.insert(
          body_flow.next.end(),
          std::make_move_iterator(body_flow.continues.begin()),
          std::make_move_iterator(body_flow.continues.end()));
      if (!body_flow.next.empty()) {
        const Blk latch = edit_->blk(carried_types);
        const std::vector<Val> latch_arguments = latch.arguments();
        for (const Path& active : body_flow.next) {
          auto values = carried_values(active);
          if (values.size() == carried_names.size()) {
            edit_->jump(active.block, latch, std::move(values));
          }
        }
        locals_ = loop_locals;
        for (std::size_t index = 0; index < carried_names.size(); ++index) {
          bind({carried_names[index], std::nullopt, statement.range, true},
               latch_arguments[index]);
        }
        auto [increment_tail, increment] =
            instantiate_expression(infix("+", state_name, step_name),
                                   statement.iterator->range, latch);
        if (!increment || increment->known() ||
            increment->type() != *iterator_type) {
          report("typed for increment has the wrong iterator type",
                 statement.iterator->range);
          return next(block);
        }
        std::vector<Val> next_values{*increment};
        next_values.insert(next_values.end(), latch_arguments.begin(),
                           latch_arguments.end());
        edit_->jump(increment_tail, header, std::move(next_values));
      }

      locals_ = outer_locals;
      locals_.resize(outer_scope_depth);
      residual_control_depth_ = outer_residual_depth;
      for (std::size_t index = 0; index < carried_names.size(); ++index) {
        bind({carried_names[index], std::nullopt, statement.range, true},
             exit.arguments()[index]);
      }
      return next(exit);
    }

    std::vector<Path> active{path(block)};
    Flow exits;
    for (detail::ExecVal& element : *elements) {
      if (loop_iterations_++ >= compiler_.evaluation_limits().steps) {
        report("compile-time for iteration limit exceeded", statement.range);
        break;
      }
      std::vector<Path> following;
      for (const Path& path : active) {
        restore(path);
        auto iterator = detail::stage(compiler_, element);
        if (!iterator) {
          report("for iterator cannot enter staged evaluation",
                 statement.iterator->range);
          continue;
        }
        const std::size_t outer_depth = locals_.depth();
        locals_.push();
        define_staged(statement.iterator->name, std::move(*iterator),
                      statement.iterator->range);
        loops_.push_back({});
        Flow flow = instantiate_sequence(statement.body, path.block);
        loops_.pop_back();
        trim_scopes(flow, outer_depth);
        exits.next.insert(exits.next.end(),
                          std::make_move_iterator(flow.breaks.begin()),
                          std::make_move_iterator(flow.breaks.end()));
        following.insert(following.end(),
                         std::make_move_iterator(flow.next.begin()),
                         std::make_move_iterator(flow.next.end()));
        following.insert(following.end(),
                         std::make_move_iterator(flow.continues.begin()),
                         std::make_move_iterator(flow.continues.end()));
      }
      active = std::move(following);
      if (active.empty()) {
        break;
      }
    }
    exits.next.insert(exits.next.end(), std::make_move_iterator(active.begin()),
                      std::make_move_iterator(active.end()));
    return exits;
  }

  Flow instantiate_return(std::span<const detail::ExprSyntax> expressions,
                          detail::SyntaxRange range, Blk block) {
    std::vector<Val> returned;
    if (expressions.size() != result_types_.size()) {
      report("fn return count does not match its result signature", range);
    } else {
      for (std::size_t index = 0; index < expressions.size(); ++index) {
        std::optional<Val> value;
        const auto& expression = expressions[index];
        const bool reference =
            (expression.value.kind == Mod::Expr::Kind::Reference ||
             expression.value.kind == Mod::Expr::Kind::Variable) &&
            expression.value.arguments.empty();
        if (reference) {
          value = use(expression);
        } else {
          const std::string name =
              "$return" + std::to_string(next_temporary_++);
          expected_values_.insert_or_assign(name, result_types_[index]);
          detail::StatementSyntax statement;
          statement.bindings.push_back({name, std::nullopt, expression.range});
          statement.expression = expression;
          statement.range = expression.range;
          Flow flow = instantiate_statement(statement, block);
          if (flow.next.size() == 1U && flow.breaks.empty() &&
              flow.continues.empty()) {
            restore(flow.next.front());
            block = flow.next.front().block;
            value = use(detail::LocalUseSyntax{name, expression.range});
          }
        }
        if (!value) {
          continue;
        }
        if (value->known() && value->type() != result_types_[index]) {
          value = materialize(*value, result_types_[index], block,
                              expression.range);
        }
        if (!value || value->type() != result_types_[index]) {
          report("returned value type does not match fn result " +
                     std::to_string(index),
                 expression.range);
          continue;
        }
        returned.push_back(*value);
      }
    }
    edit_->ret(block, std::move(returned));
    return {};
  }

  Flow instantiate_loop_control(detail::Control kind, detail::SyntaxRange range,
                                Blk block) {
    if (loops_.empty()) {
      report("loop control is outside a structured loop", range);
      return {};
    }
    const LoopContext& loop = loops_.back();
    const std::optional<Blk>& target = kind == detail::Control::Continue
                                           ? loop.continue_target
                                           : loop.break_target;
    if (!target) {
      return transfer(kind == detail::Control::Break, block);
    }

    std::vector<Val> carried;
    for (std::size_t index = 0; index < loop.carried_names.size(); ++index) {
      auto value = lookup(loop.carried_names[index]);
      if (!value) {
        report("loop-carried value '" + loop.carried_names[index] +
                   "' is unavailable at control transfer",
               range);
        continue;
      }
      auto materialized =
          materialize(*value, loop.carried_types[index], block, range);
      if (materialized) {
        carried.push_back(*materialized);
      }
    }
    if (carried.size() != loop.carried_names.size()) {
      return {};
    }
    edit_->jump(block, *target, std::move(carried));
    return {};
  }

  Flow instantiate_statement(const detail::StatementSyntax& statement,
                             Blk block) {
    if (statement.kind == detail::StatementSyntax::Kind::Return) {
      return instantiate_return(statement.values, statement.range, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Break) {
      return instantiate_loop_control(detail::Control::Break, statement.range,
                                      block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::Continue) {
      return instantiate_loop_control(detail::Control::Continue,
                                      statement.range, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::If) {
      return instantiate_if_statement(statement, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::While) {
      return instantiate_while(statement, block);
    }
    if (statement.kind == detail::StatementSyntax::Kind::For) {
      return instantiate_for(statement, block);
    }
    using Kind = Mod::Expr::Kind;
    const Mod::Expr& expression = statement.expression.value;
    if (expression.kind == Kind::Variable ||
        expression.kind == Kind::Reference) {
      if (statement.bindings.size() != 1U || !expression.arguments.empty()) {
        report("a value reference must bind exactly one value",
               statement.range);
        return next(block);
      }
      auto expected = expected_type(statement.bindings.front());
      if (auto value = use(statement.expression, expected)) {
        if (expected && value->type() != *expected) {
          if (value->known()) {
            value = materialize(*value, *expected, block, statement.range);
          } else {
            report("referenced value does not match its annotated type",
                   statement.range);
            value.reset();
          }
        }
        if (value) {
          bind(statement.bindings.front(), std::move(*value));
        }
      }
      return next(block);
    }
    if (expression.kind != Kind::If) {
      instantiate_call(statement, block);
      return next(block);
    }
    if (expression.arguments.size() != 3U || statement.bindings.size() != 1U) {
      report("if expression must have one condition, two values, and one "
             "result",
             statement.range);
      return next(block);
    }

    const detail::ExprSyntax condition_syntax{expression.arguments[0],
                                              statement.expression.range};
    if (known_result(condition_syntax.value, condition_syntax.range)) {
      auto condition = evaluate_known(condition_syntax);
      const auto selected =
          condition ? detail::known_boolean(*condition) : std::nullopt;
      if (!selected) {
        report("Known if condition must have type bool",
               condition_syntax.range);
        return next(block);
      }
      detail::StatementSyntax selected_statement = statement;
      selected_statement.expression.value =
          expression.arguments[*selected ? 1U : 2U];
      return instantiate_statement(selected_statement, block);
    }

    auto condition = use(condition_syntax);
    if (!condition) {
      return next(block);
    }
    const Blk yes = edit_->blk();
    const Blk no = edit_->blk();
    edit_->branch(block, *condition, yes, {}, no, {});
    ++residual_control_depth_;
    auto [true_tail, true_value] = instantiate_expression(
        expression.arguments[1], statement.expression.range, yes);
    auto [false_tail, false_value] = instantiate_expression(
        expression.arguments[2], statement.expression.range, no);
    --residual_control_depth_;
    if (!true_value || !false_value) {
      return next(block);
    }
    auto merge_type = expected_type(statement.bindings.front());
    if (true_value->known() && false_value->known() &&
        *true_value == *false_value && !merge_type) {
      const Blk merge = edit_->blk();
      edit_->jump(true_tail, merge);
      edit_->jump(false_tail, merge);
      bind(statement.bindings.front(), std::move(*true_value));
      return next(merge);
    }
    if (!merge_type) {
      if (!true_value->known()) {
        merge_type = true_value->type();
      } else if (!false_value->known()) {
        merge_type = false_value->type();
      } else if (true_value->type() == false_value->type()) {
        merge_type = true_value->type();
      }
    }
    if (!merge_type) {
      report("Known branch values need an expected mod type for "
             "materialization",
             statement.range);
      return next(block);
    }
    if ((!true_value->known() && true_value->type() != *merge_type) ||
        (!false_value->known() && false_value->type() != *merge_type)) {
      report("if branches produce different types", statement.range);
      return next(block);
    }
    auto materialized_true =
        materialize(*true_value, *merge_type, true_tail, statement.range);
    auto materialized_false =
        materialize(*false_value, *merge_type, false_tail, statement.range);
    if (!materialized_true || !materialized_false) {
      return next(block);
    }
    const Blk merge = edit_->blk({*merge_type});
    edit_->jump(true_tail, merge, {*materialized_true});
    edit_->jump(false_tail, merge, {*materialized_false});
    bind(statement.bindings.front(), merge.arguments().front());
    return next(merge);
  }

  void instantiate_evaluate_call(const detail::StatementSyntax& statement,
                                 const Mod::Expr& expression, Blk block) {
    const auto reject_results = [&] { invalidate(statement.bindings); };
    const Loc call_site = source(statement.expression.range);
    const detail::ExecuteFn execute =
        [&](Mod::FnDecl fn, std::vector<detail::ExecVal> arguments, Loc) {
          return detail::CompilerAccess::execute(compiler_, std::move(fn),
                                                 std::move(arguments),
                                                 residual_control_depth_ != 0U);
        };
    const detail::EvaluateCallArgument evaluate =
        [&](const Mod::Expr& argument,
            const Mod::ParamDecl* expected) -> std::optional<detail::ExecVal> {
      if (argument.kind == Mod::Expr::Kind::Lambda) {
        return compiler_fn(argument, statement.expression.range);
      }
      if ((argument.kind == Mod::Expr::Kind::Reference ||
           argument.kind == Mod::Expr::Kind::Variable) &&
          argument.arguments.empty()) {
        const auto local = lookup_staged(argument.text);
        const auto* known = local ? local->known_value() : nullptr;
        if (known) {
          return *known;
        }
        if (const auto mod =
                detail::visible_mod(compiler_, owner_, argument.text)) {
          return detail::store_exec_val(*mod);
        }
      }
      auto staged = evaluate_known(
          {argument, statement.expression.range},
          expected ? std::optional<Mod::ParamDecl>{*expected} : std::nullopt);
      const auto* known = staged ? staged->known_value() : nullptr;
      return known ? std::optional<detail::ExecVal>{*known} : std::nullopt;
    };
    auto results = detail::execute_call(compiler_, owner_, expression,
                                        call_site, statement.bindings.size(),
                                        {}, diagnostics_, evaluate, execute);
    if (!results) {
      reject_results();
      return;
    }
    for (std::size_t index = 0; index < results->size(); ++index) {
      auto value = detail::stage(compiler_, std::move((*results)[index]));
      const auto expected = expected_type(statement.bindings[index]);
      if (!value) {
        report("explicit compiler call result does not match binding '" +
                   statement.bindings[index].name + "'",
               statement.bindings[index].range);
        reject_results();
        return;
      }
      if (expected && value->type() != *expected) {
        const auto known = detail::ir_value(compiler_, *value);
        auto converted = known ? materialize(*known, *expected, block,
                                             statement.bindings[index].range)
                               : std::optional<Val>{};
        if (!converted) {
          reject_results();
          return;
        }
        bind(statement.bindings[index], std::move(*converted));
        continue;
      }
      bind_staged(statement.bindings[index], std::move(*value));
    }
  }

  void instantiate_call(const detail::StatementSyntax& statement, Blk block) {
    using Kind = Mod::Expr::Kind;
    const Kind expression_kind = statement.expression.value.kind;
    const bool require_known = expression_kind == Kind::Evaluate;
    const bool implicit_value =
        expression_kind == Kind::Number || expression_kind == Kind::Boolean ||
        expression_kind == Kind::String || expression_kind == Kind::List ||
        expression_kind == Kind::Reference ||
        expression_kind == Kind::Variable || expression_kind == Kind::FnType;
    auto contextual_type = statement.bindings.size() == 1U
                               ? expected_type(statement.bindings.front())
                               : std::optional<Type>{};
    if (require_known && statement.expression.value.arguments.size() == 1U &&
        statement.expression.value.arguments.front().kind == Kind::Call) {
      instantiate_evaluate_call(
          statement, statement.expression.value.arguments.front(), block);
      return;
    }
    if (expression_kind == Kind::Lambda) {
      auto value = contextual_type
                       ? inline_fn(statement.expression.value, *contextual_type,
                                   statement.expression.range)
                       : std::optional<Val>{};
      if (!value || statement.bindings.size() != 1U) {
        if (!contextual_type) {
          report("inline fn needs a callable type context",
                 statement.expression.range);
        }
        invalidate(statement.bindings);
        return;
      }
      bind(statement.bindings.front(), std::move(*value));
      return;
    }
    auto expected_domain = contextual_type
                               ? detail::type_domain(*contextual_type)
                               : std::optional<Mod::Expr>{};
    const bool can_materialize_known =
        implicit_value && statement.bindings.size() == 1U &&
        (expected_domain ||
         known_result(statement.expression.value, statement.expression.range)
             .has_value());
    if (require_known || can_materialize_known) {
      if (statement.bindings.size() != 1U) {
        report("compile-time evaluation must bind exactly one value",
               statement.range);
        return;
      }
      auto value = evaluate_known(
          statement.expression,
          expected_domain
              ? std::optional<Mod::ParamDecl>{{"result",
                                               std::move(*expected_domain),
                                               false, std::nullopt}}
              : std::nullopt);
      if (value) {
        if (contextual_type && value->type() != *contextual_type) {
          auto ir = detail::ir_value(compiler_, *value);
          auto materialized =
              ir ? materialize(*ir, *contextual_type, block, statement.range)
                 : std::optional<Val>{};
          if (materialized) {
            bind(statement.bindings.front(), std::move(*materialized));
          } else {
            invalidate(statement.bindings);
          }
        } else {
          bind_staged(statement.bindings.front(), std::move(value));
        }
      } else {
        invalidate(statement.bindings);
      }
      return;
    }
    const auto invalidate_results = [&] { invalidate(statement.bindings); };
    const Mod::Expr& expression = statement.expression.value;
    std::optional<Mod::FnDecl::Fixity> fixity;
    if (expression.kind == Kind::Prefix) {
      fixity = Mod::FnDecl::Fixity::Prefix;
    } else if (expression.kind == Kind::Infix) {
      fixity = Mod::FnDecl::Fixity::Infix;
    } else if (expression.kind == Kind::Postfix) {
      fixity = Mod::FnDecl::Fixity::Postfix;
    } else if (expression.kind != Kind::Call) {
      report("expression cannot be residualized as a call", statement.range);
      invalidate_results();
      return;
    }

    std::vector<PendingArgument> arguments;
    arguments.reserve(expression.arguments.size());
    for (const Mod::Expr& argument_expression : expression.arguments) {
      detail::ExprSyntax argument_syntax{argument_expression,
                                         statement.expression.range};
      PendingArgument argument;
      argument.order = arguments.size();
      if (argument_expression.kind == Kind::Lambda) {
        prepare_inline_fn(argument, argument_expression,
                          statement.expression.range);
      } else if ((argument_expression.kind == Kind::Reference ||
                  argument_expression.kind == Kind::Variable) &&
                 argument_expression.arguments.empty()) {
        argument.value = lookup_staged(argument_expression.text);
        if (!argument.value && declared_local(argument_expression.text)) {
          invalidate_results();
          return;
        }
        if (!argument.value && argument_expression.kind == Kind::Reference &&
            !visible_fns(argument_expression.text).empty()) {
          argument.fn = argument_expression.text;
        } else if (!argument.value) {
          auto used = use(argument_syntax);
          argument.value = used ? detail::stage(std::move(*used))
                                : std::optional<detail::StagedVal>{};
        }
      } else if (known_result(argument_expression,
                              statement.expression.range)) {
        argument.value = evaluate_known(argument_syntax);
      } else {
        argument.expression = argument_expression;
        argument.inferred_type = infer_expression_type(
            argument_expression, statement.expression.range);
      }
      if (!argument.valid()) {
        invalidate_results();
        return;
      }
      arguments.push_back(std::move(argument));
    }

    std::vector<std::optional<Type>> expected_types;
    bool invalid_expected_type = false;
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      const auto& binding = statement.bindings[index];
      if (!binding.type) {
        const auto expected = locals_.depth() == 1U
                                  ? expected_values_.find(binding.name)
                                  : expected_values_.end();
        expected_types.push_back(expected == expected_values_.end()
                                     ? std::optional<Type>{}
                                     : std::optional<Type>{expected->second});
        continue;
      }
      auto resolved = type(*binding.type);
      invalid_expected_type = !resolved || invalid_expected_type;
      expected_types.push_back(std::move(resolved));
    }
    if (invalid_expected_type) {
      report("call result names and types have different counts",
             statement.range);
      invalidate_results();
      return;
    }

    if (!fixity && declared_local(expression.text)) {
      if (std::any_of(
              expression.labels.begin(), expression.labels.end(),
              [](const std::string& label) { return !label.empty(); })) {
        report("an indirect call uses positional arguments",
               statement.expression.range);
        invalidate_results();
        return;
      }
      const auto staged_callee = lookup_staged(expression.text);
      const Val* callee =
          staged_callee ? staged_callee->residual_value() : nullptr;
      const auto inputs = callee
                              ? callee->type().get<std::vector<Type>>("inputs")
                              : std::optional<std::vector<Type>>{};
      const auto results =
          callee ? callee->type().get<std::vector<Type>>("results")
                 : std::optional<std::vector<Type>>{};
      const auto schema =
          callee ? std::optional<Mod::Symbol>{callee->type().schema().symbol()}
                 : std::optional<Mod::Symbol>{};
      if (!callee || !schema ||
          schema->mod_name() != detail::prelude_mod_name ||
          schema->local_name() != "callable" || !inputs || !results) {
        report("local value '" + expression.text + "' is not callable",
               statement.expression.range);
        invalidate_results();
        return;
      }
      if (arguments.size() != inputs->size()) {
        report("callable '" + expression.text + "' expects " +
                   std::to_string(inputs->size()) + " arguments",
               statement.expression.range);
        invalidate_results();
        return;
      }
      if (expected_types.size() != results->size() ||
          !std::equal(expected_types.begin(), expected_types.end(),
                      results->begin(),
                      [](const auto& expected, const Type& actual) {
                        return !expected || *expected == actual;
                      })) {
        report("callable '" + expression.text +
                   "' does not match the expected results",
               statement.expression.range);
        invalidate_results();
        return;
      }

      std::vector<Val> call_arguments;
      call_arguments.reserve(arguments.size());
      bool unresolved = false;
      for (std::size_t index = 0; index < arguments.size(); ++index) {
        PendingArgument& argument = arguments[index];
        if (argument.is_inline_fn()) {
          auto value = inline_fn(*argument.inline_fn, (*inputs)[index],
                                 statement.expression.range);
          argument.value = value ? detail::stage(std::move(*value))
                                 : std::optional<detail::StagedVal>{};
        } else if (argument.is_fn()) {
          auto value = fn_reference(argument.fn, statement.expression.range,
                                    (*inputs)[index]);
          argument.value = value ? detail::stage(std::move(*value))
                                 : std::optional<detail::StagedVal>{};
        } else if (argument.is_expression()) {
          auto [tail, value] = instantiate_expression(
              *argument.expression, statement.expression.range, block,
              (*inputs)[index]);
          block = tail;
          argument.value = value ? detail::stage(std::move(*value))
                                 : std::optional<detail::StagedVal>{};
        }
        auto value = argument.value
                         ? detail::ir_value(compiler_, *argument.value)
                         : std::optional<Val>{};
        if (value && value->type() != (*inputs)[index]) {
          value = materialize(*value, (*inputs)[index], block,
                              statement.expression.range);
        }
        if (!value) {
          unresolved = true;
        } else {
          call_arguments.push_back(std::move(*value));
        }
      }
      if (unresolved) {
        invalidate_results();
        return;
      }
      Op op = edit_->call(block, *callee, std::move(call_arguments), *results);
      detail::FnAccess::locate(*edit_, op, source(statement.expression.range));
      for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
        bind(statement.bindings[index], op.result(index));
      }
      return;
    }

    std::vector<Mod::FnDecl> declarations =
        fixity ? detail::visible_operators(compiler_, owner_, expression.text,
                                           *fixity)
               : visible_fns(expression.text);
    if (!declarations.empty() &&
        std::all_of(declarations.begin(), declarations.end(),
                    [](const Mod::FnDecl& declaration) {
                      return !detail::compiler_results(declaration).empty();
                    })) {
      report("compiler-domain call '" + expression.text +
                 "' requires explicit @ evaluation",
             statement.expression.range);
      invalidate_results();
      return;
    }
    std::vector<PendingCall> plans;
    const bool unique_declaration = declarations.size() == 1U;
    const std::size_t diagnostics_before_planning = diagnostics_.size();
    for (const auto& fn : declarations) {
      auto plan = plan_call(fn, expression, arguments, expected_types,
                            statement.expression.range,
                            unique_declaration && residual_control_depth_ == 0U,
                            unique_declaration ? &diagnostics_ : nullptr);
      if (plan) {
        plans.push_back(std::move(*plan));
      }
    }
    if (plans.empty()) {
      if (diagnostics_.size() == diagnostics_before_planning) {
        report("no overload of '" + expression.text +
                   "' accepts the call arguments and expected results",
               statement.expression.range);
      }
      invalidate_results();
      return;
    }
    if (plans.size() != 1U) {
      std::string message =
          fixity ? "operator '" + expression.text + "' is ambiguous between"
                 : "call to '" + expression.text + "' is ambiguous between";
      for (const PendingCall& plan : plans) {
        message += " '" + plan.fn.symbol().qualified_name() + "'";
      }
      report(std::move(message), statement.expression.range);
      invalidate_results();
      return;
    }

    PendingCall plan = std::move(plans.front());
    if (!detail::compiler_results(plan.fn).empty()) {
      report("compiler-domain call '" + plan.fn.symbol().qualified_name() +
                 "' requires explicit @ evaluation",
             statement.expression.range);
      invalidate_results();
      return;
    }
    const auto parameters = plan.fn.inputs();
    struct PlannedArgument {
      PendingArgument* argument;
      Type type;
    };
    std::vector<PlannedArgument> planned_arguments;
    std::size_t argument_index = 0;
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (!detail::is_value_port(parameters[index])) {
        continue;
      }
      for (PendingArgument& argument : plan.arguments[index]) {
        planned_arguments.push_back(
            {&argument, plan.partial_types.arguments[argument_index++]});
      }
    }
    std::sort(planned_arguments.begin(), planned_arguments.end(),
              [](const PlannedArgument& lhs, const PlannedArgument& rhs) {
                return lhs.argument->order < rhs.argument->order;
              });
    bool unresolved = false;
    for (const PlannedArgument& planned : planned_arguments) {
      PendingArgument& argument = *planned.argument;
      if (argument.is_inline_fn()) {
        auto value = inline_fn(*argument.inline_fn, planned.type,
                               statement.expression.range);
        argument.value = value ? detail::stage(std::move(*value))
                               : std::optional<detail::StagedVal>{};
        unresolved = !argument.value || unresolved;
      } else if (argument.is_fn()) {
        auto reference =
            fn_reference(argument.fn, statement.expression.range, planned.type);
        argument.value = reference ? detail::stage(std::move(*reference))
                                   : std::optional<detail::StagedVal>{};
        unresolved = !argument.value || unresolved;
      } else if (argument.is_expression()) {
        auto [argument_tail, value] = instantiate_expression(
            *argument.expression, statement.expression.range, block,
            planned.type);
        block = argument_tail;
        argument.value = value ? detail::stage(std::move(*value))
                               : std::optional<detail::StagedVal>{};
        unresolved = !argument.value || unresolved;
      }
    }
    if (unresolved) {
      invalidate_results();
      return;
    }

    std::vector<Val> call_arguments;
    for (const auto& parameter_arguments : plan.arguments) {
      for (const PendingArgument& argument : parameter_arguments) {
        if (!argument.value) {
          invalidate_results();
          return;
        }
        auto value = detail::ir_value(compiler_, *argument.value);
        if (!value) {
          report("call argument cannot enter Residual IR",
                 statement.expression.range);
          invalidate_results();
          return;
        }
        call_arguments.push_back(std::move(*value));
      }
    }
    Op op = edit_->call(std::move(block), plan.fn, std::move(call_arguments),
                        plan.partial_types.results);
    detail::FnAccess::locate(*edit_, op, source(statement.expression.range));
    if (statement.bindings.size() != op.results().size()) {
      report("call result count does not match its bindings", statement.range);
      invalidate_results();
      return;
    }
    for (std::size_t index = 0; index < statement.bindings.size(); ++index) {
      bind(statement.bindings[index], op.result(index));
    }
  }

  Compiler& compiler_;
  std::optional<Mod::FnDecl> declaration_;
  const detail::FnBody& body_;
  std::string owner_;
  Diag& diagnostics_;
  std::size_t initial_diagnostics_ = 0;
  std::optional<Fn> fn_;
  std::optional<Fn::Edit> edit_;
  detail::Locals locals_;
  std::unordered_map<std::string, Type> expected_values_;
  std::vector<Type> result_types_;
  std::unordered_map<std::string, Blk> blocks_;
  std::vector<LoopContext> loops_;
  std::size_t next_temporary_ = 0;
  std::size_t residual_control_depth_ = 0;
  std::size_t loop_iterations_ = 0;
  std::vector<Val> supplied_known_;
  detail::KnownBindings supplied_bindings_;
  std::vector<std::pair<std::string, Type>> inline_arguments_;
  std::optional<std::vector<Type>> inline_results_;
};

}  // namespace

namespace detail {

std::optional<Fn> instantiate_fn(Compiler& compiler, Mod::FnDecl fn,
                                 const FnBody& body, Diag& diagnostics,
                                 std::vector<Val> known_arguments,
                                 KnownBindings bindings) {
  return Instantiator(compiler, std::move(fn), body, diagnostics,
                      std::move(known_arguments), std::move(bindings))
      .instantiate();
}

std::optional<Fn>
instantiate_lambda(Compiler& compiler, std::string_view owner,
                   const Mod::Expr& expression, const Loc& source,
                   Diag& diagnostics, const KnownBindings& bindings,
                   std::optional<std::vector<Type>> expected_inputs,
                   std::optional<std::vector<Type>> expected_results,
                   bool allow_guarded_evaluation,
                   std::vector<std::pair<std::string, Type>> captures) {
  using Kind = Mod::Expr::Kind;
  const auto report = [&](std::string message) {
    diagnostics.report(std::move(message), source);
  };
  const std::size_t parameter_count = expression.labels.size();
  const bool inferred = expression.arguments.size() == parameter_count + 1U;
  const bool annotated = expression.arguments.size() == parameter_count + 2U;
  if (expression.kind != Kind::Lambda || (!inferred && !annotated)) {
    report("malformed inline fn");
    return std::nullopt;
  }
  if (expected_inputs && expected_inputs->size() != expression.labels.size()) {
    report("inline fn does not match its callable context");
    return std::nullopt;
  }

  const Mod::ParamDecl expected_type{"lambda parameter type",
                                     domain_expression(ValKind::Type), false,
                                     std::nullopt};
  std::vector<std::pair<std::string, Type>> arguments;
  arguments.reserve(expression.labels.size());
  for (std::size_t index = 0; index < expression.labels.size(); ++index) {
    auto value = evaluate_known_expression(
        compiler, owner, expression.arguments[index], expected_type, bindings,
        diagnostics, source, allow_guarded_evaluation);
    const Type* annotation = value ? value->as_type() : nullptr;
    if (annotation == nullptr ||
        (expected_inputs && *annotation != (*expected_inputs)[index])) {
      report("inline fn parameter '" + expression.labels[index] +
             "' does not match its callable context");
      return std::nullopt;
    }
    arguments.emplace_back(expression.labels[index], *annotation);
  }
  for (auto& capture : captures) {
    if (std::find_if(arguments.begin(), arguments.end(), [&](const auto& item) {
          return item.first == capture.first;
        }) != arguments.end()) {
      report("inline fn capture '" + capture.first +
             "' conflicts with a parameter");
      return std::nullopt;
    }
    arguments.push_back(std::move(capture));
  }

  if (annotated) {
    auto value = evaluate_known_expression(
        compiler, owner, expression.arguments[parameter_count], expected_type,
        bindings, diagnostics, source, allow_guarded_evaluation);
    const Type* annotation = value ? value->as_type() : nullptr;
    if (annotation == nullptr ||
        (expected_results &&
         (*expected_results != std::vector<Type>{*annotation}))) {
      report("inline fn result does not match its callable context");
      return std::nullopt;
    }
    expected_results = std::vector<Type>{*annotation};
  }

  FnBody body;
  body.source = source.source;
  body.range = {source.begin, source.end};
  BlkSyntax entry;
  entry.name = "entry";
  entry.range = body.range;
  StatementSyntax returned;
  returned.kind = StatementSyntax::Kind::Return;
  returned.range = body.range;
  returned.values.push_back({expression.arguments.back(), body.range});
  entry.statements.push_back(std::move(returned));
  body.blocks.push_back(std::move(entry));

  return Instantiator(compiler, std::string(owner), body, diagnostics,
                      std::move(arguments), std::move(expected_results),
                      bindings)
      .instantiate();
}

}  // namespace detail
}  // namespace joggle
