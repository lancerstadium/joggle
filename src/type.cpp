#include "joggle/type.h"

#include "domain.h"
#include "joggle/digest.h"
#include "type_internal.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace joggle {
namespace {

using detail::ParameterValue;

template <typename T>
std::shared_ptr<const detail::ParameterValueStorage> store(T value) {
  return std::make_shared<const detail::ParameterValueStorage>(
      detail::ParameterPayload{std::move(value)});
}

std::string encode(std::string_view tag, std::string_view value) {
  return std::string(tag) + ":" + std::to_string(value.size()) + ":" +
         std::string(value);
}

std::optional<ParameterValue::Kind> expected_kind(detail::ValueKind kind) {
  switch (kind) {
  case detail::ValueKind::Integer:
    return ParameterValue::Kind::I64;
  case detail::ValueKind::Real:
    return ParameterValue::Kind::F64;
  case detail::ValueKind::Boolean:
    return ParameterValue::Kind::Boolean;
  case detail::ValueKind::String:
    return ParameterValue::Kind::String;
  case detail::ValueKind::Type:
    return ParameterValue::Kind::Type;
  case detail::ValueKind::Attribute:
    return ParameterValue::Kind::Attribute;
  case detail::ValueKind::Function:
  case detail::ValueKind::Bytes:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ParameterValue>
literal_value(const Module::Expression& expression, detail::ValueKind kind) {
  using Kind = Module::Expression::Kind;
  if (kind == detail::ValueKind::Integer && expression.kind == Kind::Number) {
    std::int64_t value = 0;
    const auto result = std::from_chars(expression.text.data(),
                                        expression.text.data() + expression.text.size(),
                                        value);
    if (result.ec == std::errc{} &&
        result.ptr == expression.text.data() + expression.text.size()) {
      return ParameterValue(value);
    }
  }
  if (kind == detail::ValueKind::Real && expression.kind == Kind::Number) {
    std::istringstream stream(expression.text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (stream && stream.eof() && std::isfinite(value)) {
      return ParameterValue(value);
    }
  }
  if (kind == detail::ValueKind::Boolean && expression.kind == Kind::Boolean) {
    return ParameterValue(expression.text == "true");
  }
  if (kind == detail::ValueKind::String && expression.kind == Kind::String) {
    return ParameterValue(expression.text);
  }
  return std::nullopt;
}

std::string instance_name(const Module::Symbol& schema,
                          std::span<const ParameterValue> parameters) {
  std::string encoding;
  for (const ParameterValue& parameter : parameters) {
    const std::string item = parameter.canonical();
    encoding += std::to_string(item.size()) + ":" + item;
  }
  return schema.stable_name() + "/instance/" + sha256(encoding);
}

}  // namespace

bool detail::matches_parameter(const Module::ParameterDecl& schema,
                               const ParameterValue& value) {
  const auto domain = kernel_domain(schema.domain);
  if (!domain) {
    return false;
  }
  const auto expected = expected_kind(domain->element);
  if (!expected) {
    return false;
  }
  if (!domain->list) {
    return value.kind() == *expected &&
           (*expected != ParameterValue::Kind::F64 ||
            std::isfinite(*value.as_f64()));
  }
  if (value.kind() != ParameterValue::Kind::List) {
    return false;
  }
  return std::all_of(value.elements().begin(), value.elements().end(),
                     [&](const ParameterValue& element) {
                       return element.kind() == *expected &&
                              (*expected != ParameterValue::Kind::F64 ||
                               std::isfinite(*element.as_f64()));
                     });
}

std::optional<ParameterValue>
detail::parameter_default(const Module::ParameterDecl& schema) {
  if (!schema.default_value) {
    return std::nullopt;
  }
  const auto domain = kernel_domain(schema.domain);
  if (!domain || domain->list) {
    return std::nullopt;
  }
  return literal_value(*schema.default_value, domain->element);
}

std::optional<std::vector<ParameterValue>> detail::validate_parameters(
    std::string_view owner, std::span<const Module::ParameterDecl> schema,
    std::span<const ParameterValue> provided, Diagnostics& diagnostics) {
  if (provided.size() > schema.size()) {
    diagnostics.report("'" + std::string(owner) + "' expects at most " +
                       std::to_string(schema.size()) + " parameters, but " +
                       std::to_string(provided.size()) + " were provided");
    return std::nullopt;
  }
  std::vector<ParameterValue> values(provided.begin(), provided.end());
  for (std::size_t index = 0; index < provided.size(); ++index) {
    if (!matches_parameter(schema[index], provided[index])) {
      diagnostics.report("parameter '" + schema[index].name + "' of '" +
                         std::string(owner) + "' has the wrong kind");
      return std::nullopt;
    }
  }
  for (std::size_t index = provided.size(); index < schema.size(); ++index) {
    if (!schema[index].default_value) {
      diagnostics.report("missing parameter '" + schema[index].name +
                         "' for '" + std::string(owner) + "'");
      return std::nullopt;
    }
    const auto value = parameter_default(schema[index]);
    if (!value) {
      diagnostics.report("default value of parameter '" + schema[index].name +
                         "' for '" + std::string(owner) +
                         "' cannot be evaluated");
      return std::nullopt;
    }
    values.push_back(*value);
  }
  return values;
}

Type detail::TypeAccess::make(Module::TypeDecl schema,
                              std::vector<ParameterValue> parameters,
                              std::vector<ParameterValue> derived_parameters) {
  const std::string stable = instance_name(schema.symbol(), parameters);
  return Type(std::make_shared<const TypeStorage>(
      TypeStorage{std::move(schema), std::move(parameters),
                  std::move(derived_parameters), stable}));
}

Attribute detail::TypeAccess::make(Module::AttributeDecl schema,
                                   std::vector<ParameterValue> parameters) {
  const std::string stable = instance_name(schema.symbol(), parameters);
  return Attribute(std::make_shared<const AttributeStorage>(
      AttributeStorage{std::move(schema), std::move(parameters), stable}));
}

std::span<const ParameterValue>
detail::TypeAccess::parameters(const Type& type) {
  return type.parameters();
}

std::span<const ParameterValue>
detail::TypeAccess::derived_parameters(const Type& type) {
  return type.storage_->derived_parameters;
}

std::span<const ParameterValue>
detail::TypeAccess::parameters(const Attribute& attribute) {
  return attribute.parameters();
}

Type::Type(std::shared_ptr<const detail::TypeStorage> storage)
    : storage_(std::move(storage)) {}

Module::TypeDecl Type::schema() const { return storage_->schema; }

std::span<const ParameterValue> Type::parameters() const {
  return storage_->parameters;
}

std::span<const ParameterValue> Type::derived_parameters() const {
  return storage_->derived_parameters;
}

std::string_view Type::stable_name() const { return storage_->stable_name; }

bool Type::operator==(const Type& other) const {
  return stable_name() == other.stable_name();
}

Attribute::Attribute(std::shared_ptr<const detail::AttributeStorage> storage)
    : storage_(std::move(storage)) {}

Module::AttributeDecl Attribute::schema() const { return storage_->schema; }

std::span<const ParameterValue> Attribute::parameters() const {
  return storage_->parameters;
}

std::string_view Attribute::stable_name() const {
  return storage_->stable_name;
}

bool Attribute::operator==(const Attribute& other) const {
  return stable_name() == other.stable_name();
}

ParameterValue::ParameterValue(std::int64_t value) : storage_(store(value)) {}

ParameterValue::ParameterValue(double value) : storage_(store(value)) {}

ParameterValue::ParameterValue(bool value) : storage_(store(value)) {}

ParameterValue::ParameterValue(std::string value)
    : storage_(store(std::move(value))) {}

ParameterValue::ParameterValue(const char* value)
    : ParameterValue(std::string(value)) {}

ParameterValue::ParameterValue(Type value)
    : storage_(store(std::move(value))) {}

ParameterValue::ParameterValue(Attribute value)
    : storage_(store(std::move(value))) {}

ParameterValue::ParameterValue(
    std::shared_ptr<const detail::ParameterValueStorage> storage)
    : storage_(std::move(storage)) {}

ParameterValue ParameterValue::list(std::vector<ParameterValue> values) {
  return ParameterValue(store(std::move(values)));
}

ParameterValue::Kind ParameterValue::kind() const {
  if (std::holds_alternative<std::int64_t>(storage_->payload)) {
    return Kind::I64;
  }
  if (std::holds_alternative<double>(storage_->payload)) {
    return Kind::F64;
  }
  if (std::holds_alternative<bool>(storage_->payload)) {
    return Kind::Boolean;
  }
  if (std::holds_alternative<std::string>(storage_->payload)) {
    return Kind::String;
  }
  if (std::holds_alternative<Type>(storage_->payload)) {
    return Kind::Type;
  }
  if (std::holds_alternative<Attribute>(storage_->payload)) {
    return Kind::Attribute;
  }
  return Kind::List;
}

std::span<const ParameterValue> ParameterValue::elements() const {
  const auto* values =
      std::get_if<std::vector<ParameterValue>>(&storage_->payload);
  return values == nullptr ? std::span<const ParameterValue>{} : *values;
}

const std::int64_t* ParameterValue::as_i64() const {
  return std::get_if<std::int64_t>(&storage_->payload);
}

const double* ParameterValue::as_f64() const {
  return std::get_if<double>(&storage_->payload);
}

const bool* ParameterValue::as_bool() const {
  return std::get_if<bool>(&storage_->payload);
}

const std::string* ParameterValue::as_string() const {
  return std::get_if<std::string>(&storage_->payload);
}

const Type* ParameterValue::as_type() const {
  return std::get_if<Type>(&storage_->payload);
}

const Attribute* ParameterValue::as_attribute() const {
  return std::get_if<Attribute>(&storage_->payload);
}

std::string ParameterValue::canonical() const {
  return std::visit(
      [](const auto& value) -> std::string {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, std::int64_t>) {
          return "i64:" + std::to_string(value);
        } else if constexpr (std::is_same_v<T, double>) {
          std::ostringstream output;
          output << "f64:" << std::hex << std::setw(16) << std::setfill('0')
                 << std::bit_cast<std::uint64_t>(value);
          return output.str();
        } else if constexpr (std::is_same_v<T, bool>) {
          return value ? "bool:1" : "bool:0";
        } else if constexpr (std::is_same_v<T, std::string>) {
          return encode("string", value);
        } else if constexpr (std::is_same_v<T, Type>) {
          return encode("type", value.stable_name());
        } else if constexpr (std::is_same_v<T, Attribute>) {
          return encode("attr", value.stable_name());
        } else {
          std::string result = "list:" + std::to_string(value.size()) + ":";
          for (const ParameterValue& element : value) {
            const std::string item = element.canonical();
            result += std::to_string(item.size()) + ":" + item;
          }
          return result;
        }
      },
      storage_->payload);
}

bool ParameterValue::operator==(const ParameterValue& other) const {
  return canonical() == other.canonical();
}

}  // namespace joggle
