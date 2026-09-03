#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  std::ifstream prelude_input(JOGGLE_PRELUDE_MODULE);
  std::ostringstream prelude_text;
  prelude_text << prelude_input.rdbuf();
  joggle::Diagnostics prelude_diagnostics;
  const auto source_prelude = joggle::parse_module(
      prelude_text.str(), prelude_diagnostics, JOGGLE_PRELUDE_MODULE);

  joggle::Compiler compiler;
  compiler.add(R"(
joggle 1;

module native_test@1.0.0 {
  type packed(bits: int) : prelude.scalar {
    storage_bits = bits;
  }
  type word(width: int);

  fn identity<T: prelude.scalar>(input: T) -> T;
  fn encode<T: prelude.scalar>(input: T) -> word<T.storage_bits>;

  fn integers(x: i32, y: u32) -> i32 {
    result = identity(x);
    return result;
  }
  fn floating(x: f32) -> f32 {
    result = identity(x);
    return result;
  }
  fn custom(x: packed<4>) -> packed<4> {
    result = identity(x);
    return result;
  }
  fn native_width(x: i32) -> word<32> {
    result = encode(x);
    return result;
  }
  fn custom_width(x: packed<4>) -> word<4> {
    result = encode(x);
    return result;
  }
}
)",
               "native-test.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto i32 = compiler.make("i32");
  const auto integer_value_type = compiler.make("int");
  const auto type_value_type = compiler.make("type");
  const auto u32 = compiler.make("u32");
  const auto f32 = compiler.make("f32");
  const auto embedded_prelude = compiler.module("prelude");
  const auto list_schema =
      embedded_prelude ? embedded_prelude->type("list") : std::nullopt;
  const auto integer_list =
      list_schema && integer_value_type
          ? compiler.make(*list_schema, *integer_value_type)
          : std::nullopt;
  const auto known_integer =
      integer_value_type ? compiler.known(*integer_value_type, std::int64_t{42})
                         : std::nullopt;
  const auto same_known_integer =
      integer_value_type ? compiler.known(*integer_value_type, std::int64_t{42})
                         : std::nullopt;
  const auto known_i32 =
      i32 ? compiler.known(*i32, std::int64_t{7}) : std::nullopt;
  const auto integers = compiler.materialize("native_test.integers");
  const auto floating = compiler.materialize("native_test.floating");
  const auto custom = compiler.materialize("native_test.custom");
  const auto native_width = compiler.materialize("native_test.native_width");
  const auto custom_width = compiler.materialize("native_test.custom_width");
  const auto scalar =
      embedded_prelude ? embedded_prelude->interface("scalar") : std::nullopt;
  const auto storage_bits = scalar && !scalar->fields().empty()
                                ? std::optional{scalar->fields().front()}
                                : std::nullopt;

  bool ok = true;
  ok &= expect(source_prelude && embedded_prelude &&
                   source_prelude->digest() == embedded_prelude->digest(),
               "the installed Prelude source is the embedded authority");
  ok &= expect(
      i32 && u32 && f32 && integer_value_type && type_value_type &&
          integer_list &&
          integer_value_type->schema().symbol().qualified_name() ==
              "prelude.int" &&
          type_value_type->schema().symbol().qualified_name() ==
              "prelude.type" &&
          integer_list->get<joggle::Type>("element") == integer_value_type &&
          i32->schema().symbol().qualified_name() == "prelude.i32" &&
          u32->schema().symbol().qualified_name() == "prelude.u32" &&
          f32->schema().symbol().qualified_name() == "prelude.f32" &&
          storage_bits && i32->schema().derived_parameters().size() == 1U &&
          i32->schema().derived_parameters().front().name == "storage_bits",
      "compiler values and fixed-width scalars use ordinary "
      "reflected Prelude declarations");
  ok &= expect(integers && floating && custom && native_width && custom_width,
               "native and interface-conforming custom types instantiate");
  ok &=
      expect(known_integer && same_known_integer && known_i32 &&
                 known_integer->known() && !known_i32->is_function_argument() &&
                 known_integer->type() == *integer_value_type &&
                 known_integer->get<std::int64_t>() == 42 &&
                 *known_integer == *same_known_integer,
             "one Value model represents typed Known compiler values");
  const auto native_bits = native_width ? native_width->entry()
                                              .terminator()
                                              .returned()
                                              .front()
                                              .type()
                                              .get<std::int64_t>("width")
                                        : std::nullopt;
  const auto custom_bits = custom_width ? custom_width->entry()
                                              .terminator()
                                              .returned()
                                              .front()
                                              .type()
                                              .get<std::int64_t>("width")
                                        : std::nullopt;
  ok &= expect(native_bits == std::optional<std::int64_t>{32} &&
                   custom_bits == std::optional<std::int64_t>{4},
               "one interface field computes parameters for native and "
               "custom scalar types");
  const std::string text =
      integers ? joggle::format(*integers, "integers") : std::string{};
  ok &= expect(text.find("arg0: i32") != std::string::npos &&
                   text.find("arg1: u32") != std::string::npos &&
                   text.find("prelude.i32") == std::string::npos,
               "Prelude types retain their compact source spelling");
  ok &= expect(compiler.modules().size() == 1U,
               "the ambient Prelude is not a repository dependency");

  joggle::Compiler primitives;
  primitives.add(R"(
joggle 1;
module primitive_test@1.0.0 {
  type word(width: int);

  fn identity<T: type>(input: T) -> T;

  fn fold<S: list<int>>(values: S) -> int {
    total = 0;
    for value in S {
      if value > 0 && value != 2 {
        total = total + ceildiv(value, 2);
      }
    }
    return total;
  }

  fn predicate(lhs: int, rhs: int) -> bool {
    return !(lhs >= rhs) || lhs == rhs;
  }

  fn real_math(lhs: real, rhs: real) -> real {
    return min(lhs + rhs, max(lhs, rhs)) // 1.0;
  }

  fn ascending(stop: int) -> list<int> {
    return range(stop);
  }

  fn descending() -> list<int> {
    return range(5, -1, -2);
  }

  fn empty_range() -> list<int> {
    return range(4, 0);
  }

  fn reverse(values: list<int>) -> list<int> {
    result: list<int> = [];
    for index in range(length(values)) {
      result = append(result, at(values, length(values) - index - 1));
    }
    return result;
  }

  fn reverse_types(values: list<type>) -> list<type> {
    result: list<type> = [];
    for index in range(length(values)) {
      result = append(result, at(values, length(values) - index - 1));
    }
    return result;
  }

  fn append_name(values: list<string>, name: string) -> list<string> {
    return append(values, name);
  }

  fn unroll<N: int>(count: N, input: word<8>) -> word<8> {
    current = input;
    for index in range(N) {
      current = identity(current);
    }
    return current;
  }
}
)",
                 "primitive-test.joggle");
  const bool primitives_linked = primitives.link();
  const auto folded =
      primitives_linked
          ? primitives.run<std::int64_t>(
                "primitive_test.fold", std::vector<std::int64_t>{0, 1, 2, 3, 8})
          : std::nullopt;
  const auto predicate =
      primitives_linked ? primitives.run<bool>("primitive_test.predicate",
                                               std::int64_t{3}, std::int64_t{4})
                        : std::nullopt;
  const auto real_math =
      primitives_linked
          ? primitives.run<double>("primitive_test.real_math", 2.5, 4.0)
          : std::nullopt;
  const auto ascending = primitives_linked
                             ? primitives.run<std::vector<std::int64_t>>(
                                   "primitive_test.ascending", std::int64_t{4})
                             : std::nullopt;
  const auto descending = primitives_linked
                              ? primitives.run<std::vector<std::int64_t>>(
                                    "primitive_test.descending")
                              : std::nullopt;
  const auto empty_range = primitives_linked
                               ? primitives.run<std::vector<std::int64_t>>(
                                     "primitive_test.empty_range")
                               : std::nullopt;
  const auto reversed =
      primitives_linked
          ? primitives.run<std::vector<std::int64_t>>(
                "primitive_test.reverse", std::vector<std::int64_t>{2, 4, 6})
          : std::nullopt;
  const auto primitive_module = primitives.module("primitive_test");
  const auto i8 = primitives.make("i8");
  const auto f16 = primitives.make("f16");
  const auto reversed_types = primitives_linked && i8 && f16
                                  ? primitives.run<std::vector<joggle::Type>>(
                                        "primitive_test.reverse_types",
                                        std::vector<joggle::Type>{*i8, *f16})
                                  : std::nullopt;
  const auto names =
      primitives_linked ? primitives.run<std::vector<std::string>>(
                              "primitive_test.append_name",
                              std::vector<std::string>{}, std::string{"weight"})
                        : std::nullopt;
  const auto unroll_decl =
      primitive_module ? primitive_module->function("unroll") : std::nullopt;
  const auto integer_type = primitives.make("int");
  const auto count = integer_type
                         ? primitives.known(*integer_type, std::int64_t{3})
                         : std::nullopt;
  const auto unrolled = unroll_decl && count
                            ? primitives.materialize(*unroll_decl, {*count})
                            : std::nullopt;
  ok &= expect(
      primitives_linked && folded == std::optional<std::int64_t>{7} &&
          predicate == std::optional<bool>{true} &&
          real_math == std::optional<double>{4.0} && ascending &&
          *ascending == std::vector<std::int64_t>({0, 1, 2, 3}) && descending &&
          *descending == std::vector<std::int64_t>({5, 3, 1}) && empty_range &&
          empty_range->empty() && reversed &&
          *reversed == std::vector<std::int64_t>({6, 4, 2}) && reversed_types &&
          *reversed_types == std::vector<joggle::Type>({*f16, *i8}) &&
          names == std::optional<std::vector<std::string>>{{"weight"}} &&
          unrolled && unrolled->instructions().size() == 3U,
      "Prelude fn primitives drive generic for, typed compile-time "
      "lists, control, arithmetic, comparisons, and logic");

  joggle::Compiler shadowing;
  shadowing.add(R"(
joggle 1;
module shadowing@1.0.0 {
  fn ceildiv(lhs: int, rhs: int) -> int {
    return 99;
  }
  fn custom_add(lhs: int, rhs: int) -> int as + {
    return lhs - rhs;
  }
  fn local_call(lhs: int, rhs: int) -> int {
    return ceildiv(lhs, rhs);
  }
  fn local_operator(lhs: int, rhs: int) -> int {
    return lhs + rhs;
  }
  fn standard_call(lhs: int, rhs: int) -> int {
    return prelude.ceildiv(lhs, rhs);
  }
}
)",
                "shadowing.joggle");
  const bool shadowing_linked = shadowing.link();
  const auto local_call =
      shadowing_linked
          ? shadowing.run<std::int64_t>("shadowing.local_call", std::int64_t{7},
                                        std::int64_t{3})
          : std::nullopt;
  const auto local_operator =
      shadowing_linked
          ? shadowing.run<std::int64_t>("shadowing.local_operator",
                                        std::int64_t{7}, std::int64_t{3})
          : std::nullopt;
  const auto standard_call =
      shadowing_linked
          ? shadowing.run<std::int64_t>("shadowing.standard_call",
                                        std::int64_t{7}, std::int64_t{3})
          : std::nullopt;
  ok &= expect(shadowing_linked &&
                   local_call == std::optional<std::int64_t>{99} &&
                   local_operator == std::optional<std::int64_t>{4} &&
                   standard_call == std::optional<std::int64_t>{3},
               "local names and signatures shadow ambient Prelude fn while "
               "explicit qualification remains available");

  joggle::Compiler invalid_arithmetic;
  invalid_arithmetic.add(R"(
joggle 1;
module invalid_arithmetic@1.0.0 {
  fn divide_by_zero() -> int {
    return 1 / 0;
  }
}
)",
                         "invalid-arithmetic.joggle");
  const bool invalid_linked = invalid_arithmetic.link();
  const auto invalid_result = invalid_linked
                                  ? invalid_arithmetic.run<std::int64_t>(
                                        "invalid_arithmetic.divide_by_zero")
                                  : std::nullopt;
  const bool reports_division_by_zero = std::any_of(
      invalid_arithmetic.diagnostics().entries().begin(),
      invalid_arithmetic.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("compile-time division by zero") !=
               std::string::npos;
      });
  ok &= expect(invalid_linked && !invalid_result && reports_division_by_zero,
               "Prelude primitive failures preserve deterministic diagnostics");

  joggle::Compiler overflowing;
  overflowing.add(R"(
joggle 1;
module overflowing@1.0.0 {
  fn add_past_i64() -> int {
    return 9223372036854775807 + 1;
  }
}
)",
                  "overflowing.joggle");
  const bool overflowing_linked = overflowing.link();
  const auto overflowing_result =
      overflowing_linked
          ? overflowing.run<std::int64_t>("overflowing.add_past_i64")
          : std::nullopt;
  const bool reports_overflow = std::any_of(
      overflowing.diagnostics().entries().begin(),
      overflowing.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("compile-time integer arithmetic "
                                       "overflow") != std::string::npos;
      });
  ok &= expect(overflowing_linked && !overflowing_result && reports_overflow,
               "Prelude integer primitives reject overflow deterministically");

  joggle::Compiler invalid_range;
  invalid_range.add(R"(
joggle 1;
module invalid_range@1.0.0 {
  fn zero_step() -> list<int> {
    return range(0, 4, 0);
  }
}
)",
                    "invalid-range.joggle");
  const bool invalid_range_linked = invalid_range.link();
  const auto invalid_range_result =
      invalid_range_linked ? invalid_range.run<std::vector<std::int64_t>>(
                                 "invalid_range.zero_step")
                           : std::nullopt;
  const bool reports_zero_step = std::any_of(
      invalid_range.diagnostics().entries().begin(),
      invalid_range.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("range step cannot be zero") !=
               std::string::npos;
      });
  ok &=
      expect(invalid_range_linked && !invalid_range_result && reports_zero_step,
             "range rejects a zero step deterministically");

  joggle::Compiler bounded_range({4, 64});
  bounded_range.add(R"(
joggle 1;
module bounded_range@1.0.0 {
  fn expand() -> list<int> {
    return range(8);
  }
}
)",
                    "bounded-range.joggle");
  const bool bounded_range_linked = bounded_range.link();
  const auto bounded_range_result =
      bounded_range_linked
          ? bounded_range.run<std::vector<std::int64_t>>("bounded_range.expand")
          : std::nullopt;
  const bool reports_range_limit = std::any_of(
      bounded_range.diagnostics().entries().begin(),
      bounded_range.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find(
                   "range exceeds the compiler evaluation step limit") !=
               std::string::npos;
      });
  ok &= expect(bounded_range_linked && !bounded_range_result &&
                   reports_range_limit,
               "range allocation is bounded by the compiler evaluation "
               "budget");

  joggle::Compiler invalid_list;
  invalid_list.add(R"(
joggle 1;
module invalid_list@1.0.0 {
  fn out_of_bounds() -> int {
    return at([4, 8], 2);
  }
}
)",
                   "invalid-list.joggle");
  const bool invalid_list_linked = invalid_list.link();
  const auto invalid_list_result =
      invalid_list_linked
          ? invalid_list.run<std::int64_t>("invalid_list.out_of_bounds")
          : std::nullopt;
  const bool reports_list_bounds = std::any_of(
      invalid_list.diagnostics().entries().begin(),
      invalid_list.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("list index is out of bounds") !=
               std::string::npos;
      });
  ok &=
      expect(invalid_list_linked && !invalid_list_result && reports_list_bounds,
             "compile-time list indexing rejects an out-of-bounds index");

  const auto unknown = compiler.make("i33");
  ok &= expect(!unknown, "unknown Prelude type spellings are rejected");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
