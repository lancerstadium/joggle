#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "joggle/mod.h"

namespace joggle::detail {

enum class ValKind {
  Integer,
  Real,
  Boolean,
  String,
  Type,
  Fn,
  Mod,
  Bytes,
};

struct Domain {
  ValKind element = ValKind::Integer;
  bool list = false;

  bool operator==(const Domain&) const = default;
};

std::string_view domain_name(ValKind kind);
Mod::Expr domain_expression(ValKind kind, bool list = false);
std::optional<Domain> compiler_domain(const Mod::Expr& expression);
bool is_domain(const Mod::Expr& expression, ValKind kind, bool list = false);
std::optional<std::string> canonical_real(double value);

}  // namespace joggle::detail
