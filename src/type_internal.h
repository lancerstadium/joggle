#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/type.h"

namespace joggle::detail {

using ParameterPayload =
    std::variant<std::int64_t, double, bool, std::string, Type, Attribute,
                 std::vector<ParameterValue>>;

struct ParameterValueStorage {
  explicit ParameterValueStorage(ParameterPayload value)
      : payload(std::move(value)) {}
  ParameterPayload payload;
};

struct TypeStorage {
  Module::TypeDecl schema;
  std::vector<ParameterValue> parameters;
  std::string stable_name;
};

struct AttributeStorage {
  Module::AttributeDecl schema;
  std::vector<ParameterValue> parameters;
  std::string stable_name;
};

struct TypeAccess {
  static Type make(Module::TypeDecl schema,
                   std::vector<ParameterValue> parameters);
  static Attribute make(Module::AttributeDecl schema,
                        std::vector<ParameterValue> parameters);
  static std::span<const ParameterValue> parameters(const Type& type);
  static std::span<const ParameterValue> parameters(const Attribute& attribute);
};

bool matches_parameter(const Module::ParameterDecl& schema,
                       const ParameterValue& value);
std::optional<std::vector<ParameterValue>> validate_parameters(
    std::string_view owner, std::span<const Module::ParameterDecl> schema,
    std::span<const ParameterValue> provided, Diagnostics& diagnostics);

}  // namespace joggle::detail
