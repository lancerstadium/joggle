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
    std::variant<std::int64_t, double, bool, std::string, Type,
                 std::vector<ParameterValue>>;

struct ParameterValueStorage {
  explicit ParameterValueStorage(ParameterPayload value)
      : payload(std::move(value)) {}
  ParameterPayload payload;
};

struct TypeStorage {
  Module::TypeDecl schema;
  std::vector<ParameterValue> parameters;
  std::vector<ParameterValue> derived_parameters;
  std::string stable_name;
};

struct TypeAccess {
  static Type make(Module::TypeDecl schema,
                   std::vector<ParameterValue> parameters,
                   std::vector<ParameterValue> derived_parameters = {});
  static std::span<const ParameterValue> parameters(const Type& type);
  static std::span<const ParameterValue> derived_parameters(const Type& type);
};

bool matches_parameter(const Module::ParameterDecl& schema,
                       const ParameterValue& value);
std::optional<ParameterValue>
parameter_default(const Module::ParameterDecl& schema);
std::optional<std::vector<ParameterValue>> validate_parameters(
    std::string_view owner, std::span<const Module::ParameterDecl> schema,
    std::span<const ParameterValue> provided, Diagnostics& diagnostics);

}  // namespace joggle::detail
