#include "execution.h"

#include "ir_internal.h"
#include "prelude.h"
#include "type_internal.h"

#include <algorithm>
#include <stdexcept>
#include <typeinfo>
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

}  // namespace

StagedValue::StagedValue(Type type, ExecutionValue value)
    : type_(std::move(type)), value_(std::move(value)) {}

StagedValue::StagedValue(Value value)
    : type_(value.type()), value_(std::move(value)) {
  if (std::get<Value>(value_).known()) {
    throw std::invalid_argument(
        "a Known IR value must enter staging through stage(Value)");
  }
}

bool StagedValue::known() const {
  return std::holds_alternative<ExecutionValue>(value_);
}

const Type& StagedValue::type() const { return type_; }

const ExecutionValue* StagedValue::known_value() const {
  return std::get_if<ExecutionValue>(&value_);
}

ExecutionValue* StagedValue::known_value() {
  return std::get_if<ExecutionValue>(&value_);
}

const Value* StagedValue::residual_value() const {
  return std::get_if<Value>(&value_);
}

void Locals::push() { scopes_.emplace_back(); }

void Locals::pop() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Locals::resize(std::size_t depth) { scopes_.resize(depth); }

std::size_t Locals::depth() const { return scopes_.size(); }

bool Locals::define(std::string name, std::optional<StagedValue> value) {
  return !scopes_.empty() &&
         scopes_.back().emplace(std::move(name), std::move(value)).second;
}

bool Locals::assign(std::string_view name,
                    std::optional<StagedValue> value) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      found->second = std::move(value);
      return true;
    }
  }
  return false;
}

bool Locals::contains(std::string_view name) const {
  return std::any_of(scopes_.rbegin(), scopes_.rend(),
                     [&](const Scope& scope) {
                       return scope.contains(std::string(name));
                     });
}

StagedValue* Locals::find(std::string_view name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return found->second ? &*found->second : nullptr;
    }
  }
  return nullptr;
}

const StagedValue* Locals::find(std::string_view name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return found->second ? &*found->second : nullptr;
    }
  }
  return nullptr;
}

KnownBindings Locals::known_bindings() const {
  KnownBindings bindings;
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    for (const auto& [name, value] : *scope) {
      if (bindings.contains(name) || !value || !value->known()) {
        continue;
      }
      const ExecutionValue* known = value->known_value();
      if (known != nullptr) {
        if (auto payload = parameter_value(*known)) {
          bindings.emplace(name, std::move(*payload));
        }
      }
    }
  }
  return bindings;
}

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

std::optional<Type> domain_type(Compiler& compiler,
                                const Module::Expression& expression) {
  const auto domain = kernel_domain(expression);
  if (!domain) {
    return std::nullopt;
  }
  if (!domain->list) {
    return compiler.make(domain_name(domain->element));
  }
  if (expression.arguments.size() != 1U) {
    return std::nullopt;
  }
  auto element = domain_type(compiler, expression.arguments.front());
  const auto prelude = compiler.module(prelude_module_name);
  const auto list = prelude ? prelude->type("list")
                            : std::optional<Module::TypeDecl>{};
  return element && list ? compiler.make(*list, *element)
                         : std::optional<Type>{};
}

std::optional<Module::Expression> type_domain(const Type& type) {
  const Module::Symbol symbol = type.schema().symbol();
  if (symbol.module_name() != prelude_module_name) {
    return std::nullopt;
  }
  if (symbol.local_name() != "list") {
    const auto expression = Module::Expression::reference(
        std::string(symbol.local_name()));
    return kernel_domain(expression)
               ? std::optional<Module::Expression>{expression}
               : std::nullopt;
  }
  const auto parameters = TypeAccess::parameters(type);
  const Type* element = parameters.size() == 1U
                            ? parameters.front().as_type()
                            : nullptr;
  auto domain = element ? type_domain(*element)
                        : std::optional<Module::Expression>{};
  return domain ? std::optional<Module::Expression>{
                      Module::Expression::list_domain(std::move(*domain))}
                : std::nullopt;
}

std::optional<Type> execution_type(Compiler& compiler,
                                   const ExecutionValue& value) {
  if (const auto* host = std::get_if<HostValue>(&value)) {
    return host->concrete_type;
  }
  const auto domain = cpp_value_domain(execution_value_type(value));
  return domain ? domain_type(
                      compiler,
                      domain_expression(domain->element, domain->list))
                : std::optional<Type>{};
}

std::optional<StagedValue> stage(Compiler& compiler, ExecutionValue value) {
  auto type = execution_type(compiler, value);
  return type ? std::optional<StagedValue>{
                    std::in_place, std::move(*type), std::move(value)}
              : std::nullopt;
}

std::optional<StagedValue> stage(Value value) {
  if (!value.known()) {
    return StagedValue(std::move(value));
  }
  const auto domain = type_domain(value.type());
  const auto payload = FunctionAccess::known_value(value);
  const Module::ParameterDecl parameter{
      "value", domain ? *domain : Module::Expression{}, false, std::nullopt};
  auto known = domain && payload ? execution_value(*payload, parameter)
                                 : std::optional<ExecutionValue>{};
  return known ? std::optional<StagedValue>{
                     std::in_place, value.type(), std::move(*known)}
               : std::nullopt;
}

std::optional<Value> ir_value(Compiler& compiler, const StagedValue& value) {
  if (const Value* residual = value.residual_value()) {
    return *residual;
  }
  const ExecutionValue* known = value.known_value();
  auto payload = known ? parameter_value(*known)
                       : std::optional<ParameterValue>{};
  return payload ? compiler.known(value.type(), std::move(*payload))
                 : std::optional<Value>{};
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

}  // namespace joggle::detail
