#include "sema/domain.h"

#include <array>
#include <charconv>
#include <string>
#include <utility>

namespace joggle::detail {

std::string_view domain_name(ValKind kind) {
  switch (kind) {
  case ValKind::Integer:
    return "int";
  case ValKind::Real:
    return "real";
  case ValKind::Boolean:
    return "bool";
  case ValKind::String:
    return "string";
  case ValKind::Type:
    return "type";
  case ValKind::Fn:
    return "fn";
  case ValKind::Mod:
    return "mod";
  case ValKind::Bytes:
    return "bytes";
  }
  return {};
}

Mod::Expr domain_expression(ValKind kind, bool list) {
  auto element = Mod::Expr::reference(std::string(domain_name(kind)));
  return list ? Mod::Expr::list_domain(std::move(element)) : std::move(element);
}

std::optional<Domain> kernel_domain(const Mod::Expr& expression) {
  const Mod::Expr* element = &expression;
  bool list = false;
  if (expression.kind == Mod::Expr::Kind::Reference &&
      expression.text == "list" && expression.arguments.size() == 1U) {
    list = true;
    element = &expression.arguments.front();
  }
  if (element->kind != Mod::Expr::Kind::Reference ||
      !element->arguments.empty()) {
    return std::nullopt;
  }
  constexpr std::array kinds{
      ValKind::Integer, ValKind::Real, ValKind::Boolean, ValKind::String,
      ValKind::Type,    ValKind::Fn,   ValKind::Mod,     ValKind::Bytes,
  };
  for (const ValKind kind : kinds) {
    if (element->text == domain_name(kind)) {
      return Domain{kind, list};
    }
  }
  return std::nullopt;
}

bool is_domain(const Mod::Expr& expression, ValKind kind, bool list) {
  return kernel_domain(expression) == Domain{kind, list};
}

std::optional<std::string> canonical_real(double value) {
  char text[64];
  const auto formatted = std::to_chars(text, text + sizeof(text), value,
                                       std::chars_format::general);
  return formatted.ec == std::errc{}
             ? std::optional<std::string>{std::string(text, formatted.ptr)}
             : std::nullopt;
}

}  // namespace joggle::detail
