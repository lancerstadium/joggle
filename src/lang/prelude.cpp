#include "lang/prelude.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace joggle::detail {

bool is_prelude_type(std::string_view name) {
  constexpr std::array names{
      std::string_view{"type"},     std::string_view{"int"},
      std::string_view{"real"},     std::string_view{"bool"},
      std::string_view{"string"},   std::string_view{"bytes"},
      std::string_view{"fn"},       std::string_view{"mod"},
      std::string_view{"callable"}, std::string_view{"effect"},
      std::string_view{"list"},     std::string_view{"i1"},
      std::string_view{"i8"},       std::string_view{"i16"},
      std::string_view{"i32"},      std::string_view{"i64"},
      std::string_view{"u8"},       std::string_view{"u16"},
      std::string_view{"u32"},      std::string_view{"u64"},
      std::string_view{"f16"},      std::string_view{"bf16"},
      std::string_view{"f32"},      std::string_view{"f64"},
      std::string_view{"index"},
  };
  for (const std::string_view candidate : names) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

bool is_effect_type(const Type& type) {
  const Mod::Symbol symbol = type.schema().symbol();
  return symbol.mod_name() == prelude_mod_name &&
         symbol.local_name() == "effect" &&
         type.get<Type>("domain").has_value();
}

std::string_view display_type_name(std::string_view qualified_name) {
  constexpr std::string_view prefix = "prelude.";
  if (qualified_name.starts_with(prefix)) {
    const std::string_view local = qualified_name.substr(prefix.size());
    if (is_prelude_type(local)) {
      return local;
    }
  }
  return qualified_name;
}

namespace {

std::optional<std::int64_t> checked_add(std::int64_t left, std::int64_t right) {
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if ((right > 0 && left > maximum - right) ||
      (right < 0 && left < minimum - right)) {
    return std::nullopt;
  }
  return left + right;
}

std::optional<std::int64_t> checked_subtract(std::int64_t left,
                                             std::int64_t right) {
  constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
  constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
  if ((right > 0 && left < minimum + right) ||
      (right < 0 && left > maximum + right)) {
    return std::nullopt;
  }
  return left - right;
}

std::uint64_t magnitude(std::int64_t value) {
  return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1U
                   : static_cast<std::uint64_t>(value);
}

std::optional<std::int64_t> checked_multiply(std::int64_t left,
                                             std::int64_t right) {
  const bool negative = (left < 0) != (right < 0);
  const std::uint64_t left_magnitude = magnitude(left);
  const std::uint64_t right_magnitude = magnitude(right);
  constexpr std::uint64_t negative_limit = std::uint64_t{1} << 63U;
  constexpr std::uint64_t positive_limit =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  const std::uint64_t limit = negative ? negative_limit : positive_limit;
  if (right_magnitude != 0U && left_magnitude > limit / right_magnitude) {
    return std::nullopt;
  }
  const std::uint64_t product = left_magnitude * right_magnitude;
  if (!negative) {
    return static_cast<std::int64_t>(product);
  }
  if (product == negative_limit) {
    return std::numeric_limits<std::int64_t>::min();
  }
  return -static_cast<std::int64_t>(product);
}

bool invalid_integer_division(std::int64_t left, std::int64_t right) {
  return right == 0 ||
         (left == std::numeric_limits<std::int64_t>::min() && right == -1);
}

std::optional<std::int64_t>
integer_binary(std::string_view name, std::int64_t left, std::int64_t right) {
  if (name == "+") {
    return checked_add(left, right);
  }
  if (name == "-") {
    return checked_subtract(left, right);
  }
  if (name == "*") {
    return checked_multiply(left, right);
  }
  if (name == "/" || name == "//" || name == "%") {
    if (invalid_integer_division(left, right)) {
      return std::nullopt;
    }
    if (name == "%") {
      return left % right;
    }
    std::int64_t quotient = left / right;
    if (name == "//" && left % right != 0 && (left < 0) != (right < 0)) {
      --quotient;
    }
    return quotient;
  }
  return std::nullopt;
}

bool primitive_name(std::string_view name) {
  constexpr std::array names{
      std::string_view{"+"},     std::string_view{"-"},
      std::string_view{"*"},     std::string_view{"/"},
      std::string_view{"//"},    std::string_view{"%"},
      std::string_view{"<"},     std::string_view{"<="},
      std::string_view{">"},     std::string_view{">="},
      std::string_view{"=="},    std::string_view{"!="},
      std::string_view{"!"},     std::string_view{"&&"},
      std::string_view{"||"},    std::string_view{"ceildiv"},
      std::string_view{"min"},   std::string_view{"max"},
      std::string_view{"range"}, std::string_view{"length"},
      std::string_view{"repeat"}, std::string_view{"[]"},
      std::string_view{"append"},
  };
  return std::find(names.begin(), names.end(), name) != names.end();
}

const std::string& prelude_declaration_digest() {
  static const std::string value = [] {
    Diag diagnostics;
    auto mod =
        parse_mod(prelude_mod_source(), diagnostics, "<embedded-prelude>");
    if (!mod) {
      throw std::logic_error("embedded Prelude is not a valid Mod");
    }
    return std::string(mod->declaration_digest());
  }();
  return value;
}

}  // namespace

bool is_prelude_primitive(const Mod::FnDecl& fn) {
  return fn.symbol().mod_name() == prelude_mod_name &&
         fn.symbol().declaration_digest() == prelude_declaration_digest() &&
         primitive_name(fn.name());
}

std::optional<ParamVal> evaluate_prelude_primitive(
    const Mod::FnDecl& fn, std::span<const ParamVal> arguments,
    Diag& diagnostics, std::size_t element_limit, std::optional<Loc> source) {
  const auto fail = [&](std::string message) -> std::optional<ParamVal> {
    diagnostics.report(std::move(message), source);
    return std::nullopt;
  };
  if (!is_prelude_primitive(fn)) {
    return fail("fn '" + fn.symbol().qualified_name() +
                "' is not a Prelude primitive");
  }

  const std::string_view name = fn.name();
  if (name == "range") {
    if (arguments.empty() || arguments.size() > 3U) {
      return fail("Prelude range expects one, two, or three ints");
    }
    std::array<std::int64_t, 3> values{0, 0, 1};
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      const auto* value = arguments[index].as_i64();
      if (value == nullptr) {
        return fail("Prelude range expects only int arguments");
      }
      values[index] = *value;
    }
    const std::int64_t start = arguments.size() == 1U ? 0 : values[0];
    const std::int64_t stop = arguments.size() == 1U ? values[0] : values[1];
    const std::int64_t step = arguments.size() == 3U ? values[2] : 1;
    if (step == 0) {
      return fail("range step cannot be zero");
    }
    std::vector<ParamVal> result;
    std::int64_t current = start;
    while (step > 0 ? current < stop : current > stop) {
      if (result.size() >= element_limit) {
        return fail("range exceeds the compiler evaluation step limit");
      }
      result.emplace_back(current);
      const auto next = checked_add(current, step);
      if (!next) {
        break;
      }
      current = *next;
    }
    return ParamVal::list(std::move(result));
  }
  if (name == "length") {
    if (arguments.size() != 1U || arguments[0].kind() != ParamVal::Kind::List) {
      return fail("Prelude length expects one list");
    }
    const std::size_t size = arguments[0].elements().size();
    if (size >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
      return fail("list length does not fit in int");
    }
    return ParamVal(static_cast<std::int64_t>(size));
  }
  if (name == "repeat") {
    const Type* value =
        arguments.size() == 2U ? arguments[0].as_type() : nullptr;
    const auto* count =
        arguments.size() == 2U ? arguments[1].as_i64() : nullptr;
    if (value == nullptr || count == nullptr || *count < 0) {
      return fail("Prelude repeat expects a type and a non-negative int");
    }
    if (static_cast<std::uint64_t>(*count) > element_limit) {
      return fail("repeat exceeds the compiler evaluation step limit");
    }
    std::vector<ParamVal> result(static_cast<std::size_t>(*count),
                                 ParamVal(*value));
    return ParamVal::list(std::move(result));
  }
  if (name == "[]") {
    const auto* index =
        arguments.size() == 2U ? arguments[1].as_i64() : nullptr;
    if (arguments.size() != 2U || arguments[0].kind() != ParamVal::Kind::List ||
        index == nullptr) {
      return fail("list subscript expects a list and an int index");
    }
    const auto elements = arguments[0].elements();
    if (*index < 0 || static_cast<std::uint64_t>(*index) >= elements.size()) {
      return fail("list index is out of bounds");
    }
    return elements[static_cast<std::size_t>(*index)];
  }
  if (name == "append") {
    if (arguments.size() != 2U || arguments[0].kind() != ParamVal::Kind::List) {
      return fail("Prelude append expects a list and one value");
    }
    const auto elements = arguments[0].elements();
    if (elements.size() >= element_limit) {
      return fail("append exceeds the compiler evaluation step limit");
    }
    std::vector<ParamVal> result(elements.begin(), elements.end());
    result.push_back(arguments[1]);
    return ParamVal::list(std::move(result));
  }
  if (name == "!") {
    const bool* value =
        arguments.size() == 1U ? arguments[0].as_bool() : nullptr;
    return value ? std::optional<ParamVal>{ParamVal(!*value)}
                 : fail("Prelude ! expects one bool");
  }
  if (name == "&&" || name == "||") {
    const bool* left =
        arguments.size() == 2U ? arguments[0].as_bool() : nullptr;
    const bool* right =
        arguments.size() == 2U ? arguments[1].as_bool() : nullptr;
    if (left == nullptr || right == nullptr) {
      return fail("Prelude " + std::string(name) + " expects two bools");
    }
    return ParamVal(name == "&&" ? *left && *right : *left || *right);
  }

  if ((name == "+" || name == "-") && arguments.size() == 1U) {
    if (arguments.size() != 1U) {
      return fail("Prelude " + std::string(name) + " expects one value");
    }
    if (const auto* integer = arguments[0].as_i64()) {
      if (name == "-" && *integer == std::numeric_limits<std::int64_t>::min()) {
        return fail("compile-time integer arithmetic overflow");
      }
      return ParamVal(name == "-" ? -*integer : *integer);
    }
    if (const auto* real = arguments[0].as_f64()) {
      const double result = name == "-" ? -*real : *real;
      return std::isfinite(result)
                 ? std::optional<ParamVal>{ParamVal(result)}
                 : fail("compile-time floating-point arithmetic is not finite");
    }
    return fail("Prelude " + std::string(name) + " expects one int or real");
  }

  if (arguments.size() != 2U) {
    return fail("Prelude " + std::string(name) + " expects two values");
  }
  if (const auto* left = arguments[0].as_i64()) {
    const auto* right = arguments[1].as_i64();
    if (right == nullptr) {
      return fail("Prelude " + std::string(name) +
                  " received values from different domains");
    }
    if (name == "<") {
      return ParamVal(*left < *right);
    }
    if (name == "<=") {
      return ParamVal(*left <= *right);
    }
    if (name == ">") {
      return ParamVal(*left > *right);
    }
    if (name == ">=") {
      return ParamVal(*left >= *right);
    }
    if (name == "==") {
      return ParamVal(*left == *right);
    }
    if (name == "!=") {
      return ParamVal(*left != *right);
    }
    if (name == "min") {
      return ParamVal(std::min(*left, *right));
    }
    if (name == "max") {
      return ParamVal(std::max(*left, *right));
    }
    if (name == "ceildiv") {
      if (*left < 0 || *right <= 0) {
        return fail(
            "ceildiv requires a non-negative dividend and positive divisor");
      }
      return ParamVal(*left / *right + (*left % *right != 0 ? 1 : 0));
    }
    const auto result = integer_binary(name, *left, *right);
    if (result) {
      return ParamVal(*result);
    }
    return fail((name == "/" || name == "//" || name == "%") && *right == 0
                    ? "compile-time division by zero"
                    : "compile-time integer arithmetic overflow");
  }
  if (const auto* left = arguments[0].as_f64()) {
    const auto* right = arguments[1].as_f64();
    if (right == nullptr) {
      return fail("Prelude " + std::string(name) +
                  " received values from different domains");
    }
    if (name == "<") {
      return ParamVal(*left < *right);
    }
    if (name == "<=") {
      return ParamVal(*left <= *right);
    }
    if (name == ">") {
      return ParamVal(*left > *right);
    }
    if (name == ">=") {
      return ParamVal(*left >= *right);
    }
    if (name == "==") {
      return ParamVal(*left == *right);
    }
    if (name == "!=") {
      return ParamVal(*left != *right);
    }
    if (name == "min") {
      return ParamVal(std::min(*left, *right));
    }
    if (name == "max") {
      return ParamVal(std::max(*left, *right));
    }
    if ((name == "/" || name == "//") && *right == 0.0) {
      return fail("compile-time division by zero");
    }
    const double result = name == "+"   ? *left + *right
                          : name == "-" ? *left - *right
                          : name == "*" ? *left * *right
                          : name == "/" ? *left / *right
                          : name == "//"
                              ? std::floor(*left / *right)
                              : std::numeric_limits<double>::quiet_NaN();
    return std::isfinite(result)
               ? std::optional<ParamVal>{ParamVal(result)}
               : fail("compile-time floating-point arithmetic is not finite");
  }
  if (const auto* left = arguments[0].as_bool()) {
    const auto* right = arguments[1].as_bool();
    if (right != nullptr && (name == "==" || name == "!=")) {
      return ParamVal(name == "==" ? *left == *right : *left != *right);
    }
  }
  if (const auto* left = arguments[0].as_string()) {
    const auto* right = arguments[1].as_string();
    if (right != nullptr && (name == "==" || name == "!=")) {
      return ParamVal(name == "==" ? *left == *right : *left != *right);
    }
  }
  return fail("Prelude " + std::string(name) +
              " received values from an unsupported domain");
}

}  // namespace joggle::detail
