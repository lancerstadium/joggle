#include "prelude.h"

#include "prelude_runtime.h"

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
      std::string_view{"type"},   std::string_view{"int"},
      std::string_view{"real"},   std::string_view{"bool"},
      std::string_view{"string"}, std::string_view{"bytes"},
      std::string_view{"function"},
      std::string_view{"module"}, std::string_view{"callable"},
      std::string_view{"list"},   std::string_view{"i1"},
      std::string_view{"i8"},     std::string_view{"i16"},
      std::string_view{"i32"},    std::string_view{"i64"},
      std::string_view{"u8"},     std::string_view{"u16"},
      std::string_view{"u32"},    std::string_view{"u64"},
      std::string_view{"f16"},    std::string_view{"bf16"},
      std::string_view{"f32"},    std::string_view{"f64"},
      std::string_view{"index"},
  };
  for (const std::string_view candidate : names) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
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
      std::string_view{"+"},       std::string_view{"-"},
      std::string_view{"*"},       std::string_view{"/"},
      std::string_view{"//"},      std::string_view{"%"},
      std::string_view{"<"},       std::string_view{"<="},
      std::string_view{">"},       std::string_view{">="},
      std::string_view{"=="},      std::string_view{"!="},
      std::string_view{"!"},       std::string_view{"&&"},
      std::string_view{"||"},      std::string_view{"ceildiv"},
      std::string_view{"min"},     std::string_view{"max"},
      std::string_view{"range"},   std::string_view{"length"},
      std::string_view{"at"},      std::string_view{"append"},
  };
  return std::find(names.begin(), names.end(), name) != names.end();
}

const std::string& prelude_declaration_digest() {
  static const std::string value = [] {
    Diagnostics diagnostics;
    auto module = parse_module(prelude_module_source(), diagnostics,
                               "<embedded-prelude>");
    if (!module) {
      throw std::logic_error("embedded Prelude is not a valid Module");
    }
    return std::string(module->declaration_digest());
  }();
  return value;
}

}  // namespace

bool is_prelude_primitive(const Module::FunctionDecl& function) {
  return function.symbol().module_name() == prelude_module_name &&
         function.symbol().declaration_digest() == prelude_declaration_digest() &&
         primitive_name(function.name());
}

std::optional<ParameterValue>
evaluate_prelude_primitive(const Module::FunctionDecl& function,
                           std::span<const ParameterValue> arguments,
                           Diagnostics& diagnostics, std::size_t element_limit,
                           std::optional<SourceRange> source) {
  const auto fail = [&](std::string message) -> std::optional<ParameterValue> {
    diagnostics.report(std::move(message), source);
    return std::nullopt;
  };
  if (!is_prelude_primitive(function)) {
    return fail("function '" + function.symbol().qualified_name() +
                "' is not a Prelude primitive");
  }

  const std::string_view name = function.name();
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
    std::vector<ParameterValue> result;
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
    return ParameterValue::list(std::move(result));
  }
  if (name == "length") {
    if (arguments.size() != 1U ||
        arguments[0].kind() != ParameterValue::Kind::List) {
      return fail("Prelude length expects one list");
    }
    const std::size_t size = arguments[0].elements().size();
    if (size >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
      return fail("list length does not fit in int");
    }
    return ParameterValue(static_cast<std::int64_t>(size));
  }
  if (name == "at") {
    const auto* index =
        arguments.size() == 2U ? arguments[1].as_i64() : nullptr;
    if (arguments.size() != 2U ||
        arguments[0].kind() != ParameterValue::Kind::List || index == nullptr) {
      return fail("Prelude at expects a list and an int index");
    }
    const auto elements = arguments[0].elements();
    if (*index < 0 || static_cast<std::uint64_t>(*index) >= elements.size()) {
      return fail("list index is out of bounds");
    }
    return elements[static_cast<std::size_t>(*index)];
  }
  if (name == "append") {
    if (arguments.size() != 2U ||
        arguments[0].kind() != ParameterValue::Kind::List) {
      return fail("Prelude append expects a list and one value");
    }
    const auto elements = arguments[0].elements();
    if (elements.size() >= element_limit) {
      return fail("append exceeds the compiler evaluation step limit");
    }
    std::vector<ParameterValue> result(elements.begin(), elements.end());
    result.push_back(arguments[1]);
    return ParameterValue::list(std::move(result));
  }
  if (name == "!") {
    const bool* value =
        arguments.size() == 1U ? arguments[0].as_bool() : nullptr;
    return value ? std::optional<ParameterValue>{ParameterValue(!*value)}
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
    return ParameterValue(name == "&&" ? *left && *right : *left || *right);
  }

  if ((name == "+" || name == "-") && arguments.size() == 1U) {
    if (arguments.size() != 1U) {
      return fail("Prelude " + std::string(name) + " expects one value");
    }
    if (const auto* integer = arguments[0].as_i64()) {
      if (name == "-" &&
          *integer == std::numeric_limits<std::int64_t>::min()) {
        return fail("compile-time integer arithmetic overflow");
      }
      return ParameterValue(name == "-" ? -*integer : *integer);
    }
    if (const auto* real = arguments[0].as_f64()) {
      const double result = name == "-" ? -*real : *real;
      return std::isfinite(result)
                 ? std::optional<ParameterValue>{ParameterValue(result)}
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
      return ParameterValue(*left < *right);
    }
    if (name == "<=") {
      return ParameterValue(*left <= *right);
    }
    if (name == ">") {
      return ParameterValue(*left > *right);
    }
    if (name == ">=") {
      return ParameterValue(*left >= *right);
    }
    if (name == "==") {
      return ParameterValue(*left == *right);
    }
    if (name == "!=") {
      return ParameterValue(*left != *right);
    }
    if (name == "min") {
      return ParameterValue(std::min(*left, *right));
    }
    if (name == "max") {
      return ParameterValue(std::max(*left, *right));
    }
    if (name == "ceildiv") {
      if (*left < 0 || *right <= 0) {
        return fail(
            "ceildiv requires a non-negative dividend and positive divisor");
      }
      return ParameterValue(*left / *right + (*left % *right != 0 ? 1 : 0));
    }
    const auto result = integer_binary(name, *left, *right);
    if (result) {
      return ParameterValue(*result);
    }
    return fail(
        (name == "/" || name == "//" || name == "%") && *right == 0
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
      return ParameterValue(*left < *right);
    }
    if (name == "<=") {
      return ParameterValue(*left <= *right);
    }
    if (name == ">") {
      return ParameterValue(*left > *right);
    }
    if (name == ">=") {
      return ParameterValue(*left >= *right);
    }
    if (name == "==") {
      return ParameterValue(*left == *right);
    }
    if (name == "!=") {
      return ParameterValue(*left != *right);
    }
    if (name == "min") {
      return ParameterValue(std::min(*left, *right));
    }
    if (name == "max") {
      return ParameterValue(std::max(*left, *right));
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
               ? std::optional<ParameterValue>{ParameterValue(result)}
               : fail("compile-time floating-point arithmetic is not finite");
  }
  if (const auto* left = arguments[0].as_bool()) {
    const auto* right = arguments[1].as_bool();
    if (right != nullptr && (name == "==" || name == "!=")) {
      return ParameterValue(name == "==" ? *left == *right
                                         : *left != *right);
    }
  }
  if (const auto* left = arguments[0].as_string()) {
    const auto* right = arguments[1].as_string();
    if (right != nullptr && (name == "==" || name == "!=")) {
      return ParameterValue(name == "==" ? *left == *right
                                         : *left != *right);
    }
  }
  return fail("Prelude " + std::string(name) +
              " received values from an unsupported domain");
}

}  // namespace joggle::detail
