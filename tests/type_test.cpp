#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
    joggle 1;
    module testing@1.0.0 {
      type label(name: string);
    }
  )",
               "testing.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto test_ir = compiler.module("test_ir");
  const auto testing = compiler.module("testing");
  const auto integer_schema =
      test_ir ? test_ir->type("integer") : std::nullopt;
  const auto label_schema = testing ? testing->type("label") : std::nullopt;
  if (!integer_schema || !label_schema) {
    return EXIT_FAILURE;
  }
  compiler.verify(*integer_schema, [](const joggle::Type&) { return true; });

  const auto first = compiler.make(*integer_schema, 8);
  const auto second = compiler.make(*integer_schema, std::int64_t{8});
  const auto label = compiler.make(*label_schema, "cpu");

  bool ok = true;
  ok &= expect(first && second && *first == *second,
               "ordinary C++ values construct one stable type identity");
  ok &= expect(first && first->get<bool>("signed") == false,
               "schema defaults are materialized");
  ok &= expect(first && first->stable_name().find("/type/integer/instance/") !=
                            std::string_view::npos,
               "type identity derives from the stable schema symbol");
  ok &= expect(label.has_value(), "metadata type construction");
  ok &= expect(first && first->get<std::int64_t>("width") == 8 &&
                   first->get<bool>("signed") == false && label &&
                   label->get<std::string>("name") == "cpu",
               "named typed access hides parameter positions and values");

  joggle::Compiler nonfinite;
  nonfinite.add(R"(
    joggle 1;
    module numeric@1.0.0 {
      type scale(value: real);
    }
  )",
                "numeric.joggle");
  const bool nonfinite_linked = nonfinite.link();
  const auto numeric = nonfinite.module("numeric");
  const auto scale = numeric ? numeric->type("scale") : std::nullopt;
  ok &= expect(
      nonfinite_linked && scale &&
          !nonfinite.make(*scale, std::numeric_limits<double>::infinity()),
      "C++ construction rejects values the text DSL cannot encode");

  compiler.add("joggle 1; module late@1.0.0 {}", "late.joggle");
  ok &= expect(!compiler.ok(), "linked compiler rejects schema mutation");

  joggle::Compiler shaped;
  shaped.load(JOGGLE_TEST_MODULE);
  shaped.add(R"(
    joggle 1;
    module shaped@1.0.0 {
      import test_ir@1;
      type tensor(element: type, shape: list<int>);
    }
  )",
             "shaped.joggle");
  if (!shaped.link()) {
    shaped.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto shaped_module = shaped.module("shaped");
  const auto tensor_schema =
      shaped_module ? shaped_module->type("tensor") : std::nullopt;
  if (!tensor_schema || !first) {
    return EXIT_FAILURE;
  }
  const std::array<std::int64_t, 2> shape{1, 32};
  const auto tensor = shaped.make(*tensor_schema, *first, shape);
  ok &= expect(tensor && tensor->get<std::vector<std::int64_t>>("shape") ==
                             std::vector<std::int64_t>({1, 32}),
               "typed construction and named list access");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
