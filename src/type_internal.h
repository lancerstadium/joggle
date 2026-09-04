#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "joggle/diag.h"
#include "joggle/type.h"

namespace joggle::detail {

using ParameterPayload = std::variant<std::int64_t, double, bool, std::string,
                                      Type, std::vector<ParamVal>>;

struct ParamValStorage {
  explicit ParamValStorage(ParameterPayload value)
      : payload(std::move(value)) {}
  ParameterPayload payload;
};

struct TypeStorage {
  Mod::TypeDecl schema;
  std::vector<ParamVal> parameters;
  std::vector<ParamVal> derived_parameters;
  std::string stable_name;
};

struct TypeAccess {
  static Type make(Mod::TypeDecl schema, std::vector<ParamVal> parameters,
                   std::vector<ParamVal> derived_parameters = {});
  static std::span<const ParamVal> parameters(const Type& type);
  static std::span<const ParamVal> derived_parameters(const Type& type);
};

bool matches_parameter(const Mod::ParamDecl& schema, const ParamVal& value);
std::optional<ParamVal> parameter_default(const Mod::ParamDecl& schema);
std::optional<std::vector<ParamVal>>
validate_parameters(std::string_view owner,
                    std::span<const Mod::ParamDecl> schema,
                    std::span<const ParamVal> provided, Diag& diagnostics);

}  // namespace joggle::detail
