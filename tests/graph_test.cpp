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
      import arith@1;
      type other();
      op source<T: type>() -> T;
      op scope(body: region);
      op scope_value<T: type>(body: region) -> T;
      op branches(left: region, right: region);
    }
  )",
               "control.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arith = compiler.module("arith");
  const auto control = compiler.module("control");
  const auto integer_schema = arith ? arith->type("integer") : std::nullopt;
  const auto add_schema = arith ? arith->operation("add") : std::nullopt;
  const auto cast_schema =
      arith ? arith->operation("cast") : std::nullopt;
  const auto scope_schema =
      control ? control->operation("scope") : std::nullopt;
  const auto scope_value_schema =
      control ? control->operation("scope_value") : std::nullopt;
  const auto branches_schema =
      control ? control->operation("branches") : std::nullopt;
  const auto source_schema =
      control ? control->operation("source") : std::nullopt;
  const auto other_schema = control ? control->type("other") : std::nullopt;
  if (!integer_schema || !add_schema || !cast_schema || !scope_schema ||
      !scope_value_schema || !branches_schema || !source_schema ||
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

    const auto scope = edit.append(*scope_schema);
    const auto body = edit.region(scope, "body");
    edit.append(body, *cast_schema, {add->result(0)});

    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*graph), "committed graph verifies");
  ok &= expect(graph->inputs().size() == 2U && graph->operations().size() == 2U,
               "root SSA structure");
  ok &= expect(graph->inputs().front().is_argument() && add &&
                   !add->result(0).is_argument() &&
                   !add->parent(),
               "a Graph directly owns its inputs and top-level operations");
  ok &= expect(graph->all_operations().size() == 3U &&
                   graph->all_operations()[0].schema() == *add_schema &&
                   graph->all_operations()[1].schema() == *scope_schema &&
                   graph->all_operations()[2].schema() == *cast_schema,
               "whole-graph traversal is preorder across nested regions");
  ok &= expect(add && add->value().type() == *integer,
               "a single-result operation exposes its value directly");
  bool valueless_rejected = false;
  try {
    graph->operations()[1].value();
  } catch (const std::logic_error& error) {
    valueless_rejected =
        std::string_view(error.what()).find("exactly one value") !=
        std::string_view::npos;
  }
  ok &= expect(valueless_rejected,
               "the value shortcut rejects a structural operation");

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
               "a result-only variable asks C++ construction for an explicit "
               "type without poisoning Compiler diagnostics");

  std::optional<joggle::Operation> inserted;
  {
    auto edit = graph->edit();
    inserted =
        edit.insert(*add, *cast_schema, {graph->inputs()[0]});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(inserted &&
                   graph->operations().front().schema().name() ==
                       "cast" &&
                   graph->operations()[1].schema().name() == "add",
               "a pass can insert a producer at an existing operation");

  std::optional<joggle::Operation> replaced_add;
  const joggle::Operation original_add = *add;
  {
    auto edit = graph->edit();
    replaced_add = edit.replace(*add, *add_schema);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(replaced_add && !original_add.valid() &&
                   replaced_add->operands().size() == 2U &&
                   replaced_add->results().size() == 1U &&
                   graph->all_operations().size() == 4U,
               "operation replacement preserves operands and result shape");
  add = replaced_add;

  std::optional<joggle::Operation> rolled_back;
  {
    auto edit = graph->edit();
    rolled_back =
        edit.append(*add_schema, {graph->inputs()[0]}, {*integer});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok() && rolled_back &&
                     !rolled_back->valid(),
                 "schema-invalid edit is rejected and rolled back immediately");
  }
  ok &= expect(rolled_back && !rolled_back->valid(),
               "handles created by a failed edit stay invalid");
  ok &= expect(graph->operations().size() == 3U,
               "rollback restores the prior graph");

  std::optional<joggle::Operation> first_cast;
  std::optional<joggle::Operation> second_cast;
  {
    auto edit = graph->edit();
    first_cast = edit.append(*cast_schema, {add->result(0)});
    second_cast =
        edit.append(*cast_schema, {first_cast->result(0)});
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
                   graph->outputs().size() == 1U &&
                   graph->outputs().front() == add->result(0) &&
                   second_cast->operands()[0].defining_operation() &&
                   second_cast->operands()[0]
                           .defining_operation()
                           ->schema()
                           .name() == "add",
               "replace updates ordinary uses and the graph boundary");

  auto structured = compiler.graph();
  std::optional<joggle::Operation> structured_scope;
  std::optional<joggle::Operation> structured_cast;
  std::optional<joggle::Region> structured_region;
  std::optional<joggle::Value> structured_argument;
  if (!structured) {
    return EXIT_FAILURE;
  }
  {
    auto edit = structured->edit();
    structured_scope = edit.append(*scope_schema);
    structured_region = edit.region(*structured_scope, "body", {*integer});
    structured_argument = structured_region->arguments().front();
    structured_cast = edit.append(*structured_region, *cast_schema,
                                      {*structured_argument});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(structured_region && structured_cast &&
                   structured_argument && structured_argument->is_argument() &&
                   structured_cast->parent() == structured_region,
               "a structured region directly owns arguments and operations");

  auto invalid_region = compiler.graph();
  if (!invalid_region) {
    return EXIT_FAILURE;
  }
  bool invalid_region_rejected = false;
  {
    auto edit = invalid_region->edit();
    const auto scope = edit.append(*scope_schema);
    edit.region(scope, "wrong");
    joggle::Diagnostics diagnostics;
    invalid_region_rejected =
        !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(invalid_region_rejected &&
                   invalid_region->operations().empty(),
               "a region must bind the named slot declared by its operation");

  {
    auto edit = structured->edit();
    edit.erase(*structured_scope);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(structured->operations().empty() && structured_scope &&
                   !structured_scope->valid() && structured_cast &&
                   !structured_cast->valid() && structured_region &&
                   !structured_region->valid() && structured_argument &&
                   !structured_argument->valid(),
               "erasing a structured operation removes its complete subtree");

  auto live_subtree = compiler.graph();
  std::optional<joggle::Operation> live_scope;
  std::optional<joggle::Operation> live_nested;
  std::optional<joggle::Operation> live_user;
  if (!live_subtree) {
    return EXIT_FAILURE;
  }
  {
    auto edit = live_subtree->edit();
    live_scope = edit.append(*scope_value_schema, {}, {*integer});
    const auto body = edit.region(*live_scope, "body", {*integer});
    const auto argument = body.arguments().front();
    live_nested = edit.append(body, *cast_schema, {argument});
    live_user = edit.append(*cast_schema, {live_scope->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  bool rejected_live_subtree = false;
  try {
    auto edit = live_subtree->edit();
    edit.erase(*live_scope);
  } catch (const std::invalid_argument& error) {
    rejected_live_subtree =
        std::string_view(error.what()).find("live result uses") !=
        std::string_view::npos;
  }
  ok &=
      expect(rejected_live_subtree && live_scope && live_scope->valid() &&
                 live_nested && live_nested->valid() && live_user &&
                 live_user->valid() && live_subtree->operations().size() == 2U &&
                 live_subtree->all_operations().size() == 3U,
             "structured erase rejects external uses before modification");

  auto sibling_use = compiler.graph();
  if (!sibling_use) {
    return EXIT_FAILURE;
  }
  bool sibling_use_rejected = false;
  {
    auto edit = sibling_use->edit();
    const auto branches = edit.append(*branches_schema);
    const auto left = edit.region(branches, "left", {*integer});
    const auto right = edit.region(branches, "right", {*integer});
    const auto produced =
        edit.append(left, *cast_schema, {left.arguments().front()});
    edit.append(right, *cast_schema, {produced.result(0)});
    joggle::Diagnostics diagnostics;
    sibling_use_rejected = !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(sibling_use_rejected && sibling_use->operations().empty(),
               "a C++ transaction cannot use a value from a sibling region");

  auto nested_insert = compiler.graph();
  if (!nested_insert) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Region> inserted_region;
  std::optional<joggle::Operation> inserted_head;
  std::optional<joggle::Operation> inserted_tail;
  {
    auto edit = nested_insert->edit();
    const auto input = edit.argument(*integer);
    const auto scope = edit.append(*scope_schema);
    inserted_region = edit.region(scope, "body");
    inserted_tail = edit.append(*inserted_region, *cast_schema, {input});
    inserted_head = edit.insert(*inserted_tail, *cast_schema, {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto nested_sequence =
      inserted_region ? inserted_region->operations()
                      : std::vector<joggle::Operation>{};
  ok &= expect(inserted_head && inserted_tail && inserted_region &&
                   nested_insert->operations().size() == 1U &&
                   nested_insert->all_operations().size() == 3U &&
                   nested_sequence.size() == 2U &&
                   nested_sequence[0] == *inserted_head &&
                   nested_sequence[1] == *inserted_tail &&
                   inserted_head->parent() == inserted_region &&
                   inserted_tail->parent() == inserted_region,
               "nested insertion preserves region order and ownership");

  std::optional<joggle::Operation> flattened;
  {
    auto edit = live_subtree->edit();
    flattened = edit.replace(*live_scope, *source_schema);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto flattened_input = live_user->operands().front();
  const auto flattened_definition = flattened_input.defining_operation();
  ok &=
      expect(flattened && flattened->valid() && live_scope &&
                 !live_scope->valid() && live_nested && !live_nested->valid() &&
                 live_user && live_user->valid() && flattened_definition &&
                 *flattened_definition == *flattened &&
                 flattened_definition->schema() == *source_schema &&
                 flattened_definition->result(0) == flattened->result(0) &&
                 live_subtree->operations().size() == 2U,
             "operation replacement can flatten a structured subtree");

  auto invalid = compiler.graph();
  if (!invalid) {
    return EXIT_FAILURE;
  }
  bool invalid_rejected = false;
  {
    auto edit = invalid->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*other);
    edit.append(*add_schema, {lhs, rhs}, {*integer});
    joggle::Diagnostics diagnostics;
    invalid_rejected = !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(invalid_rejected && invalid->inputs().empty() &&
                   invalid->operations().empty(),
               "a schema-invalid C++ transaction is rejected and rolled back");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
