#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "domain.h"
#include "joggle/compiler.h"

namespace joggle::detail {

struct FnBody;

enum class Control { Next, Return, Break, Continue, Error };

struct KnownBinding {
  ParamVal value;
  std::optional<Mod::Expr> domain;
};

using KnownBindings = std::unordered_map<std::string, KnownBinding>;

// The evaluator's one internal value domain. Known values retain their C++
// payload; Residual values retain their Fn-owned SSA handle. Both carry
// one resolved Joggle Type, so staging never changes typing semantics.
class StagedVal {
public:
  StagedVal(Type type, ExecVal value);
  explicit StagedVal(Val value);

  bool known() const;
  const Type& type() const;
  const ExecVal* known_value() const;
  ExecVal* known_value();
  const Val* residual_value() const;

private:
  Type type_;
  std::variant<ExecVal, Val> value_;
};

class Locals {
public:
  using Scope = std::unordered_map<std::string, std::optional<StagedVal>>;

  void push();
  void pop();
  void resize(std::size_t depth);
  std::size_t depth() const;
  bool define(std::string name, std::optional<StagedVal> value);
  bool assign(std::string_view name, std::optional<StagedVal> value);
  bool contains(std::string_view name) const;
  StagedVal* find(std::string_view name);
  const StagedVal* find(std::string_view name) const;
  std::vector<std::string> names() const;
  KnownBindings known_bindings() const;

private:
  std::vector<Scope> scopes_;
};

std::string_view exec_val_type(const ExecVal& value);
std::optional<Domain> cpp_value_domain(std::string_view type);
std::optional<Type> domain_type(Compiler& compiler, const Mod::Expr& domain);
std::optional<Mod::Expr> type_domain(const Type& type);
std::optional<Type> execution_type(Compiler& compiler, const ExecVal& value);
std::optional<StagedVal> stage(Compiler& compiler, ExecVal value);
std::optional<StagedVal> stage(Val value);
std::optional<Val> ir_value(Compiler& compiler, const StagedVal& value);
std::optional<bool> known_boolean(const StagedVal& value);
std::optional<std::vector<ExecVal>> list_elements(const ExecVal& value);
bool same_staged_value(const StagedVal& lhs, const StagedVal& rhs);

std::optional<ExecVal> exec_val(const ParamVal& value,
                                const Mod::ParamDecl& parameter);
std::optional<ParamVal> parameter_value(const ExecVal& value);

using ExecuteFn = std::function<std::optional<ExecVals>(
    Mod::FnDecl, std::vector<ExecVal>, SourceRange)>;
using EvaluateCallArgument = std::function<std::optional<ExecVal>(
    const Mod::Expr&, const Mod::ParamDecl*)>;

std::optional<ExecVals> execute_call(
    Compiler& compiler, std::string_view owner, const Mod::Expr& expression,
    SourceRange call_site, std::size_t result_count,
    std::span<const Mod::ParamDecl> expected_results, Diagnostics& diagnostics,
    const EvaluateCallArgument& evaluate, const ExecuteFn& execute,
    std::span<const Mod::FnDecl> declarations = {});

std::optional<ExecVals>
execute_body(Compiler& compiler, const Mod::FnDecl& fn, const FnBody& body,
             std::span<const ExecVal> arguments,
             Compiler::EvaluationLimits limits, std::size_t& steps,
             bool under_residual_control, Diagnostics& diagnostics,
             const ExecuteFn& execute);

bool verify_body_calls(Compiler& compiler, const Mod::FnDecl& fn,
                       const FnBody& body, Diagnostics& diagnostics);

}  // namespace joggle::detail
