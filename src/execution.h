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

struct FunctionBody;

enum class Control { Next, Return, Break, Continue, Error };
using KnownBindings = std::unordered_map<std::string, ParameterValue>;

// The evaluator's one internal value domain. Known values retain their C++
// payload; Residual values retain their Function-owned SSA handle. Both carry
// one resolved Joggle Type, so staging never changes typing semantics.
class StagedValue {
public:
  StagedValue(Type type, ExecutionValue value);
  explicit StagedValue(Value value);

  bool known() const;
  const Type& type() const;
  const ExecutionValue* known_value() const;
  ExecutionValue* known_value();
  const Value* residual_value() const;

private:
  Type type_;
  std::variant<ExecutionValue, Value> value_;
};

class Locals {
public:
  using Scope = std::unordered_map<std::string, std::optional<StagedValue>>;

  void push();
  void pop();
  void resize(std::size_t depth);
  std::size_t depth() const;
  bool define(std::string name, std::optional<StagedValue> value);
  bool assign(std::string_view name, std::optional<StagedValue> value);
  bool contains(std::string_view name) const;
  StagedValue* find(std::string_view name);
  const StagedValue* find(std::string_view name) const;
  KnownBindings known_bindings() const;

private:
  std::vector<Scope> scopes_;
};

std::string_view execution_value_type(const ExecutionValue& value);
std::optional<Domain> cpp_value_domain(std::string_view type);
std::optional<Type> domain_type(Compiler& compiler,
                                const Module::Expression& domain);
std::optional<Module::Expression> type_domain(const Type& type);
std::optional<Type> execution_type(Compiler& compiler,
                                   const ExecutionValue& value);
std::optional<StagedValue> stage(Compiler& compiler, ExecutionValue value);
std::optional<StagedValue> stage(Value value);
std::optional<Value> ir_value(Compiler& compiler, const StagedValue& value);
std::optional<bool> known_boolean(const StagedValue& value);
bool same_staged_value(const StagedValue& lhs, const StagedValue& rhs);

std::optional<ExecutionValue>
execution_value(const ParameterValue& value,
                const Module::ParameterDecl& parameter);
std::optional<ParameterValue>
parameter_value(const ExecutionValue& value);

using ExecuteFunction = std::function<std::optional<ExecutionValue>(
    Module::FunctionDecl, std::vector<ExecutionValue>)>;

std::optional<ExecutionValue> execute_body(
    Compiler& compiler, const Module::FunctionDecl& function,
    const FunctionBody& body, std::span<const ExecutionValue> arguments,
    Compiler::EvaluationLimits limits, std::size_t& steps,
    bool under_residual_control, Diagnostics& diagnostics,
    const ExecuteFunction& execute);

bool verify_body_calls(Compiler& compiler,
                       const Module::FunctionDecl& function,
                       const FunctionBody& body, Diagnostics& diagnostics);

}  // namespace joggle::detail
