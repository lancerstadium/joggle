#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "joggle/module.h"

namespace joggle::detail {

enum class ValueKind {
  Integer,
  Real,
  Boolean,
  String,
  Type,
  Function,
  Bytes,
};

struct Domain {
  ValueKind element = ValueKind::Integer;
  bool list = false;

  bool operator==(const Domain&) const = default;
};

std::string_view domain_name(ValueKind kind);
Module::Expression domain_expression(ValueKind kind, bool list = false);
std::optional<Domain> kernel_domain(const Module::Expression& expression);
bool is_domain(const Module::Expression& expression, ValueKind kind,
               bool list = false);
std::optional<std::string> canonical_real(double value);

}  // namespace joggle::detail
