#include "domain.h"

#include <array>
#include <charconv>
#include <string>
#include <utility>

namespace joggle::detail {

std::string_view domain_name(ValueKind kind) {
  switch (kind) {
  case ValueKind::Integer:
    return "int";
  case ValueKind::Real:
    return "real";
  case ValueKind::Boolean:
    return "bool";
  case ValueKind::String:
    return "string";
  case ValueKind::Type:
    return "type";
  case ValueKind::Attribute:
    return "attr";
  case ValueKind::Function:
    return "function";
  case ValueKind::Bytes:
    return "bytes";
  }
  return {};
}

Module::Expression domain_expression(ValueKind kind, bool list) {
  auto element = Module::Expression::reference(std::string(domain_name(kind)));
  return list ? Module::Expression::list_domain(std::move(element))
              : std::move(element);
}

std::optional<Domain> kernel_domain(const Module::Expression& expression) {
  const Module::Expression* element = &expression;
  bool list = false;
  if (expression.kind == Module::Expression::Kind::Reference &&
      expression.text == "list" && expression.arguments.size() == 1U) {
    list = true;
    element = &expression.arguments.front();
  }
  if (element->kind != Module::Expression::Kind::Reference ||
      !element->arguments.empty()) {
    return std::nullopt;
  }
  constexpr std::array kinds{
      ValueKind::Integer, ValueKind::Real,      ValueKind::Boolean,
      ValueKind::String,  ValueKind::Type,      ValueKind::Attribute,
      ValueKind::Function,   ValueKind::Bytes,
  };
  for (const ValueKind kind : kinds) {
    if (element->text == domain_name(kind)) {
      return Domain{kind, list};
    }
  }
  return std::nullopt;
}

bool is_domain(const Module::Expression& expression, ValueKind kind,
               bool list) {
  return kernel_domain(expression) == Domain{kind, list};
}

std::optional<std::string> canonical_real(double value) {
  char text[64];
  const auto formatted =
      std::to_chars(text, text + sizeof(text), value,
                    std::chars_format::general);
  return formatted.ec == std::errc{}
             ? std::optional<std::string>{std::string(text, formatted.ptr)}
             : std::nullopt;
}

}  // namespace joggle::detail
