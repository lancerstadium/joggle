#pragma once

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "joggle/module.h"

namespace joggle {

class Compiler;

namespace detail {
class ParameterValue;
struct TypeStorage;
struct AttributeStorage;
struct ParameterValueStorage;
struct TypeAccess;
template <typename T, typename Value>
std::optional<T> get_parameter(const Value& value, std::string_view name);
}  // namespace detail

class Type {
public:
  Module::TypeDecl schema() const;
  std::string_view stable_name() const;

  template <typename T> std::optional<T> get(std::string_view name) const;

  bool operator==(const Type& other) const;

private:
  std::span<const detail::ParameterValue> parameters() const;
  std::span<const detail::ParameterValue> derived_parameters() const;
  explicit Type(std::shared_ptr<const detail::TypeStorage> storage);
  std::shared_ptr<const detail::TypeStorage> storage_;

  friend class Compiler;
  friend class detail::ParameterValue;
  friend struct detail::TypeAccess;
  template <typename T, typename Value>
  friend std::optional<T> detail::get_parameter(const Value&, std::string_view);
};

class Attribute {
public:
  Module::AttributeDecl schema() const;
  std::string_view stable_name() const;

  template <typename T> std::optional<T> get(std::string_view name) const;

  bool operator==(const Attribute& other) const;

private:
  std::span<const detail::ParameterValue> parameters() const;
  explicit Attribute(std::shared_ptr<const detail::AttributeStorage> storage);
  std::shared_ptr<const detail::AttributeStorage> storage_;

  friend class Compiler;
  friend class detail::ParameterValue;
  friend struct detail::TypeAccess;
  template <typename T, typename Value>
  friend std::optional<T> detail::get_parameter(const Value&, std::string_view);
};

namespace detail {

class ParameterValue {
public:
  enum class Kind { I64, F64, Boolean, String, Type, Attribute, List };

  ParameterValue(std::int64_t value);
  ParameterValue(double value);
  ParameterValue(bool value);
  ParameterValue(std::string value);
  ParameterValue(const char* value);
  ParameterValue(Type value);
  ParameterValue(Attribute value);

  static ParameterValue list(std::vector<ParameterValue> values);

  Kind kind() const;
  std::span<const ParameterValue> elements() const;
  const std::int64_t* as_i64() const;
  const double* as_f64() const;
  const bool* as_bool() const;
  const std::string* as_string() const;
  const Type* as_type() const;
  const Attribute* as_attribute() const;
  std::string canonical() const;
  bool operator==(const ParameterValue& other) const;

private:
  explicit ParameterValue(std::shared_ptr<const ParameterValueStorage> storage);
  std::shared_ptr<const ParameterValueStorage> storage_;

  friend class Compiler;
};

template <typename> inline constexpr bool always_false = false;

template <typename> struct VectorElement;

template <typename T, typename Allocator>
struct VectorElement<std::vector<T, Allocator>> {
  using type = T;
};

template <typename T> struct IsVector : std::false_type {};

template <typename T, typename Allocator>
struct IsVector<std::vector<T, Allocator>> : std::true_type {};

template <typename T> inline constexpr bool is_vector = IsVector<T>::value;

template <typename T, typename = void> struct IsRange : std::false_type {};

template <typename T>
struct IsRange<T, std::void_t<decltype(std::begin(std::declval<T&>())),
                              decltype(std::end(std::declval<T&>()))>>
    : std::true_type {};

template <typename T> inline constexpr bool is_range = IsRange<T>::value;

template <typename T>
std::optional<T> decode_parameter(const ParameterValue& value) {
  if constexpr (std::is_same_v<T, std::int64_t>) {
    const auto* decoded = value.as_i64();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (std::is_same_v<T, double>) {
    const auto* decoded = value.as_f64();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (std::is_same_v<T, bool>) {
    const auto* decoded = value.as_bool();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (std::is_same_v<T, std::string>) {
    const auto* decoded = value.as_string();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (std::is_same_v<T, Type>) {
    const auto* decoded = value.as_type();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (std::is_same_v<T, Attribute>) {
    const auto* decoded = value.as_attribute();
    return decoded ? std::optional<T>{*decoded} : std::nullopt;
  } else if constexpr (is_vector<T>) {
    using Element = typename VectorElement<T>::type;
    if (value.kind() != ParameterValue::Kind::List) {
      return std::nullopt;
    }
    T decoded;
    decoded.reserve(value.elements().size());
    for (const ParameterValue& item : value.elements()) {
      auto element = decode_parameter<Element>(item);
      if (!element) {
        return std::nullopt;
      }
      decoded.push_back(std::move(*element));
    }
    return decoded;
  } else {
    static_assert(always_false<T>, "unsupported Joggle parameter type");
  }
}

template <typename T> Module::ParameterDecl cpp_parameter() {
  if constexpr (std::is_same_v<T, std::int64_t>) {
    return {"", Module::Expression::reference("int"), false, std::nullopt};
  } else if constexpr (std::is_same_v<T, double>) {
    return {"", Module::Expression::reference("real"), false, std::nullopt};
  } else if constexpr (std::is_same_v<T, bool>) {
    return {"", Module::Expression::reference("bool"), false, std::nullopt};
  } else if constexpr (std::is_same_v<T, std::string>) {
    return {"", Module::Expression::reference("string"), false,
            std::nullopt};
  } else if constexpr (std::is_same_v<T, Type>) {
    return {"", Module::Expression::reference("type"), false, std::nullopt};
  } else if constexpr (std::is_same_v<T, Attribute>) {
    return {"", Module::Expression::reference("attr"), false, std::nullopt};
  } else if constexpr (is_vector<T>) {
    auto result = cpp_parameter<typename VectorElement<T>::type>();
    result.domain =
        Module::Expression::list_domain(std::move(result.domain));
    return result;
  } else {
    static_assert(always_false<T>, "unsupported Joggle method type");
  }
}

template <typename T> ParameterValue encode_parameter(T&& value) {
  using Value = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<Value, ParameterValue>) {
    return std::forward<T>(value);
  } else if constexpr (std::is_same_v<Value, bool>) {
    return ParameterValue(value);
  } else if constexpr (std::is_integral_v<Value>) {
    return ParameterValue(static_cast<std::int64_t>(value));
  } else if constexpr (std::is_floating_point_v<Value>) {
    return ParameterValue(static_cast<double>(value));
  } else if constexpr (std::is_same_v<Value, std::string>) {
    return ParameterValue(std::forward<T>(value));
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
    return ParameterValue(std::string(std::string_view(value)));
  } else if constexpr (std::is_same_v<Value, Type> ||
                       std::is_same_v<Value, Attribute>) {
    return ParameterValue(std::forward<T>(value));
  } else if constexpr (is_range<Value>) {
    std::vector<ParameterValue> elements;
    for (auto&& item : value) {
      elements.push_back(encode_parameter(std::forward<decltype(item)>(item)));
    }
    return ParameterValue::list(std::move(elements));
  } else {
    static_assert(always_false<Value>, "unsupported Joggle parameter type");
  }
}

template <typename T, typename Value>
std::optional<T> get_parameter(const Value& value, std::string_view name) {
  const auto schema = value.schema().parameters();
  const auto parameters = value.parameters();
  for (std::size_t index = 0; index < schema.size(); ++index) {
    if (schema[index].name == name) {
      return index < parameters.size() ? decode_parameter<T>(parameters[index])
                                       : std::nullopt;
    }
  }
  if constexpr (std::is_same_v<Value, Type>) {
    const auto derived_schema = value.schema().derived_parameters();
    const auto derived = value.derived_parameters();
    for (std::size_t index = 0; index < derived_schema.size(); ++index) {
      if (derived_schema[index].name == name) {
        return index < derived.size() ? decode_parameter<T>(derived[index])
                                      : std::nullopt;
      }
    }
  }
  return std::nullopt;
}

}  // namespace detail

template <typename T> std::optional<T> Type::get(std::string_view name) const {
  return detail::get_parameter<T>(*this, name);
}

template <typename T>
std::optional<T> Attribute::get(std::string_view name) const {
  return detail::get_parameter<T>(*this, name);
}

}  // namespace joggle
