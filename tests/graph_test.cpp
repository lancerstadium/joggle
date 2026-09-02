#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
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
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
    joggle 1;
    module control@1.0.0 {
      type other();
      fn source<T: type>() -> T;
    }
  )", "control.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto test_ir = compiler.module("test_ir");
  const auto control = compiler.module("control");
  const auto integer_schema =
      test_ir ? test_ir->type("integer") : std::nullopt;
  const auto add_schema =
      test_ir ? test_ir->function("add") : std::nullopt;
  const auto cast_schema =
      test_ir ? test_ir->function("cast") : std::nullopt;
  const auto source_schema =
      control ? control->function("source") : std::nullopt;
  const auto other_schema = control ? control->type("other") : std::nullopt;
  if (!integer_schema || !add_schema || !cast_schema || !source_schema ||
      !other_schema) {
    return EXIT_FAILURE;
  }
  compiler.bind(*integer_schema,
                [](const joggle::Type&, joggle::Diagnostics&) { return true; });
  const auto integer = compiler.make(*integer_schema, std::int64_t{8});
  const auto other = compiler.make(*other_schema);
  auto graph = compiler.graph();
  if (!integer || !other || !graph) {
    return EXIT_FAILURE;
  }

  std::optional<joggle::Operation> add;
  {
    auto edit = graph->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*integer);
    add = edit.append(*add_schema, {lhs, rhs});
    edit.append(*cast_schema, {add->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*graph), "a committed function body verifies");
  ok &= expect(graph->inputs().size() == 2U &&
                   graph->operations().size() == 2U &&
                   graph->all_operations() == graph->operations(),
               "the transitional Graph is now strictly single-level");
  ok &= expect(add && add->value().type() == *integer &&
                   !add->result(0).is_argument(),
               "instruction results retain their inferred type");

  bool needs_explicit_result = false;
  try {
    auto edit = graph->edit();
    edit.append(*source_schema);
  } catch (const std::invalid_argument& error) {
    needs_explicit_result =
        std::string_view(error.what()).find("cannot infer type variable 'T'") !=
        std::string_view::npos;
  }
  ok &= expect(needs_explicit_result && compiler.ok(),
               "an output-only type variable needs an explicit result type");

  std::optional<joggle::Operation> inserted;
  {
    auto edit = graph->edit();
    inserted = edit.insert(*add, *cast_schema, {graph->inputs()[0]});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(inserted && graph->operations().front() == *inserted,
               "an edit inserts before an existing instruction");

  const joggle::Operation original_add = *add;
  {
    auto edit = graph->edit();
    add = edit.replace(*add, *add_schema);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(add && !original_add.valid() && add->operands().size() == 2U,
               "replacement preserves operands and invalidates the old handle");

  std::optional<joggle::Operation> rolled_back;
  {
    auto edit = graph->edit();
    rolled_back =
        edit.append(*add_schema, {graph->inputs()[0]}, {*integer});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok(),
                 "a schema-invalid transaction is rejected");
  }
  ok &= expect(rolled_back && !rolled_back->valid(),
               "rollback invalidates handles created by the failed edit");

  std::optional<joggle::Operation> first_cast;
  std::optional<joggle::Operation> second_cast;
  {
    auto edit = graph->edit();
    first_cast = edit.append(*cast_schema, {add->result(0)});
    second_cast = edit.append(*cast_schema, {first_cast->result(0)});
    edit.output(first_cast->result(0));
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  {
    auto edit = graph->edit();
    edit.replace(first_cast->result(0), add->result(0));
    edit.erase(*first_cast);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(first_cast && !first_cast->valid() && second_cast &&
                   second_cast->operands().front() == add->result(0) &&
                   graph->outputs().front() == add->result(0),
               "replace rewires instruction and boundary uses before erase");

  auto invalid = compiler.graph();
  if (!invalid) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*other);
    edit.append(*add_schema, {lhs, rhs}, {*integer});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok() &&
                     invalid->inputs().empty() &&
                     invalid->operations().empty(),
                 "a type-invalid edit rolls the complete transaction back");
  }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
