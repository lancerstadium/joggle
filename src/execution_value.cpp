#include "execution.h"

#include "type_internal.h"

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
