#include "joggle/type.h"

#include "sema/domain.h"
#include "joggle/digest.h"
#include "ir/type.h"

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

using detail::ParamVal;

template <typename T>
std::shared_ptr<const detail::ParamValStorage> store(T value) {
  return std::make_shared<const detail::ParamValStorage>(
      detail::ParameterPayload{std::move(value)});
}

std::string encode(std::string_view tag, std::string_view value) {
  return std::string(tag) + ":" + std::to_string(value.size()) + ":" +
         std::string(value);
}

std::optional<ParamVal::Kind> expected_kind(detail::ValKind kind) {
  switch (kind) {
  case detail::ValKind::Integer:
    return ParamVal::Kind::I64;
  case detail::ValKind::Real:
    return ParamVal::Kind::F64;
  case detail::ValKind::Boolean:
    return ParamVal::Kind::Boolean;
  case detail::ValKind::String:
    return ParamVal::Kind::String;
  case detail::ValKind::Type:
    return ParamVal::Kind::Type;
  case detail::ValKind::Fn:
  case detail::ValKind::Bytes:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ParamVal> literal_value(const Mod::Expr& expression,
                                      detail::ValKind kind) {
  using Kind = Mod::Expr::Kind;
  if (kind == detail::ValKind::Integer && expression.kind == Kind::Number) {
    std::int64_t value = 0;
    const auto result =
        std::from_chars(expression.text.data(),
                        expression.text.data() + expression.text.size(), value);
    if (result.ec == std::errc{} &&
        result.ptr == expression.text.data() + expression.text.size()) {
      return ParamVal(value);
    }
  }
  if (kind == detail::ValKind::Real && expression.kind == Kind::Number) {
    std::istringstream stream(expression.text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (stream && stream.eof() && std::isfinite(value)) {
      return ParamVal(value);
    }
  }
  if (kind == detail::ValKind::Boolean && expression.kind == Kind::Boolean) {
    return ParamVal(expression.text == "true");
  }
  if (kind == detail::ValKind::String && expression.kind == Kind::String) {
    return ParamVal(expression.text);
  }
  return std::nullopt;
}

std::string instance_name(const Mod::Symbol& schema,
                          std::span<const ParamVal> parameters) {
  std::string encoding;
  for (const ParamVal& parameter : parameters) {
    const std::string item = parameter.canonical();
    encoding += std::to_string(item.size()) + ":" + item;
  }
  return schema.stable_name() + "/instance/" + sha256(encoding);
}

}  // namespace

bool detail::matches_parameter(const Mod::ParamDecl& schema,
                               const ParamVal& value) {
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
           (*expected != ParamVal::Kind::F64 || std::isfinite(*value.as_f64()));
  }
  if (value.kind() != ParamVal::Kind::List) {
    return false;
  }
  return std::all_of(value.elements().begin(), value.elements().end(),
                     [&](const ParamVal& element) {
                       return element.kind() == *expected &&
                              (*expected != ParamVal::Kind::F64 ||
                               std::isfinite(*element.as_f64()));
                     });
}

std::optional<ParamVal>
detail::parameter_default(const Mod::ParamDecl& schema) {
  if (!schema.default_value) {
    return std::nullopt;
  }
  const auto domain = kernel_domain(schema.domain);
  if (!domain || domain->list) {
    return std::nullopt;
  }
  return literal_value(*schema.default_value, domain->element);
}

std::optional<std::vector<ParamVal>> detail::validate_parameters(
    std::string_view owner, std::span<const Mod::ParamDecl> schema,
    std::span<const ParamVal> provided, Diag& diagnostics) {
  if (provided.size() > schema.size()) {
    diagnostics.report("'" + std::string(owner) + "' expects at most " +
                       std::to_string(schema.size()) + " parameters, but " +
                       std::to_string(provided.size()) + " were provided");
    return std::nullopt;
  }
  std::vector<ParamVal> values(provided.begin(), provided.end());
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

Type detail::TypeAccess::make(Mod::TypeDecl schema,
                              std::vector<ParamVal> parameters,
                              std::vector<ParamVal> derived_parameters) {
  const std::string stable = instance_name(schema.symbol(), parameters);
  return Type(std::make_shared<const TypeStorage>(
      TypeStorage{std::move(schema), std::move(parameters),
                  std::move(derived_parameters), stable}));
}

std::span<const ParamVal> detail::TypeAccess::parameters(const Type& type) {
  return type.parameters();
}

std::span<const ParamVal>
detail::TypeAccess::derived_parameters(const Type& type) {
  return type.storage_->derived_parameters;
}

Type::Type(std::shared_ptr<const detail::TypeStorage> storage)
    : storage_(std::move(storage)) {}

Mod::TypeDecl Type::schema() const { return storage_->schema; }

std::span<const ParamVal> Type::parameters() const {
  return storage_->parameters;
}

std::span<const ParamVal> Type::derived_parameters() const {
  return storage_->derived_parameters;
}

std::string_view Type::stable_name() const { return storage_->stable_name; }

bool Type::operator==(const Type& other) const {
  return stable_name() == other.stable_name();
}

ParamVal::ParamVal(std::int64_t value) : storage_(store(value)) {}

ParamVal::ParamVal(double value) : storage_(store(value)) {}

ParamVal::ParamVal(bool value) : storage_(store(value)) {}

ParamVal::ParamVal(std::string value) : storage_(store(std::move(value))) {}

ParamVal::ParamVal(const char* value) : ParamVal(std::string(value)) {}

ParamVal::ParamVal(Type value) : storage_(store(std::move(value))) {}

ParamVal::ParamVal(std::shared_ptr<const detail::ParamValStorage> storage)
    : storage_(std::move(storage)) {}

ParamVal ParamVal::list(std::vector<ParamVal> values) {
  return ParamVal(store(std::move(values)));
}

ParamVal::Kind ParamVal::kind() const {
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
  return Kind::List;
}

std::span<const ParamVal> ParamVal::elements() const {
  const auto* values = std::get_if<std::vector<ParamVal>>(&storage_->payload);
  return values == nullptr ? std::span<const ParamVal>{} : *values;
}

const std::int64_t* ParamVal::as_i64() const {
  return std::get_if<std::int64_t>(&storage_->payload);
}

const double* ParamVal::as_f64() const {
  return std::get_if<double>(&storage_->payload);
}

const bool* ParamVal::as_bool() const {
  return std::get_if<bool>(&storage_->payload);
}

const std::string* ParamVal::as_string() const {
  return std::get_if<std::string>(&storage_->payload);
}

const Type* ParamVal::as_type() const {
  return std::get_if<Type>(&storage_->payload);
}

std::string ParamVal::canonical() const {
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
        } else {
          std::string result = "list:" + std::to_string(value.size()) + ":";
          for (const ParamVal& element : value) {
            const std::string item = element.canonical();
            result += std::to_string(item.size()) + ":" + item;
          }
          return result;
        }
      },
      storage_->payload);
}

bool ParamVal::operator==(const ParamVal& other) const {
  return canonical() == other.canonical();
}

}  // namespace joggle
