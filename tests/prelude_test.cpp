#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

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
      integer_value_type
          ? compiler.known(*integer_value_type, std::int64_t{42})
          : std::nullopt;
  const auto same_known_integer =
      integer_value_type
          ? compiler.known(*integer_value_type, std::int64_t{42})
          : std::nullopt;
  const auto known_i32 =
      i32 ? compiler.known(*i32, std::int64_t{7}) : std::nullopt;
  const auto integers = compiler.function("native_test.integers");
  const auto floating = compiler.function("native_test.floating");
  const auto custom = compiler.function("native_test.custom");
  const auto native_width = compiler.function("native_test.native_width");
  const auto custom_width = compiler.function("native_test.custom_width");
  const auto scalar =
      embedded_prelude ? embedded_prelude->interface("scalar") : std::nullopt;
  const auto storage_bits = scalar && !scalar->fields().empty()
                                ? std::optional{scalar->fields().front()}
                                : std::nullopt;

  bool ok = true;
  ok &= expect(source_prelude && embedded_prelude &&
                   source_prelude->digest() == embedded_prelude->digest(),
               "the installed Prelude source is the embedded authority");
  ok &= expect(i32 && u32 && f32 && integer_value_type && type_value_type &&
                   integer_list &&
                   integer_value_type->schema().symbol().qualified_name() ==
                       "prelude.int" &&
                   type_value_type->schema().symbol().qualified_name() ==
                       "prelude.type" &&
                   integer_list->get<joggle::Type>("element") ==
                       integer_value_type &&
                   i32->schema().symbol().qualified_name() == "prelude.i32" &&
                   u32->schema().symbol().qualified_name() == "prelude.u32" &&
                   f32->schema().symbol().qualified_name() == "prelude.f32" &&
                   storage_bits &&
                   i32->schema().derived_parameters().size() == 1U &&
                   i32->schema().derived_parameters().front().name ==
                       "storage_bits",
               "compiler values and fixed-width scalars use ordinary "
               "reflected Prelude declarations");
  ok &= expect(integers && floating && custom && native_width && custom_width,
               "native and interface-conforming custom types instantiate");
  ok &= expect(known_integer && same_known_integer && known_i32 &&
                   known_integer->known() && !known_i32->is_function_argument() &&
                   known_integer->type() == *integer_value_type &&
                   known_integer->get<std::int64_t>() == 42 &&
                   *known_integer == *same_known_integer,
               "one Value model represents typed Known compiler values");
  const auto native_bits =
      native_width
          ? native_width->entry().terminator().returned().front().type().get<std::int64_t>("width")
          : std::nullopt;
  const auto custom_bits =
      custom_width
          ? custom_width->entry().terminator().returned().front().type().get<std::int64_t>("width")
          : std::nullopt;
  ok &= expect(native_bits == std::optional<std::int64_t>{32} &&
                   custom_bits == std::optional<std::int64_t>{4},
               "one interface field computes parameters for native and "
               "custom scalar types");
  const std::string text = integers ? joggle::format(*integers, "integers")
                                    : std::string{};
  ok &= expect(text.find("arg0: i32") != std::string::npos &&
                   text.find("arg1: u32") != std::string::npos &&
                   text.find("prelude.i32") == std::string::npos,
               "Prelude types retain their compact source spelling");
  ok &= expect(compiler.modules().size() == 1U,
               "the ambient Prelude is not a package dependency");
  const auto unknown = compiler.make("i33");
  ok &= expect(!unknown, "unknown Prelude type spellings are rejected");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
