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
      fn add_i32(lhs: i32, rhs: i32) -> i32;
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
  const auto add_i32_schema =
      control ? control->function("add_i32") : std::nullopt;
  const auto other_schema = control ? control->type("other") : std::nullopt;
  if (!integer_schema || !add_schema || !cast_schema || !source_schema ||
      !add_i32_schema || !other_schema) {
    return EXIT_FAILURE;
  }
  compiler.bind(*integer_schema,
                [](const joggle::Type&, joggle::Diagnostics&) { return true; });
  const auto integer = compiler.make(*integer_schema, std::int64_t{8});
  const auto other = compiler.make(*other_schema);
  const auto boolean = compiler.make("i1");
  const auto i32 = compiler.make("i32");
  auto function = compiler.function();
  if (!integer || !other || !boolean || !i32 || !function) {
    return EXIT_FAILURE;
  }

  std::optional<joggle::Instruction> add;
  {
    auto edit = function->edit();
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
  ok &= expect(compiler.verify(*function), "a committed function body verifies");
  ok &= expect(function->arguments().size() == 2U &&
                   function->instructions().size() == 2U &&
                   function->instructions() == function->instructions(),
               "a Function owns one ordered instruction view across its blocks");
  ok &= expect(add && add->value().type() == *integer &&
                   !add->result(0).is_function_argument() &&
                   !add->result(0).is_block_argument(),
               "instruction results retain their inferred type");

  const auto known_seven = compiler.known(*i32, std::int64_t{7});
  auto mixed = compiler.function();
  if (!known_seven || !mixed) {
    return EXIT_FAILURE;
  }
  {
    auto edit = mixed->edit();
    const auto input = edit.argument(*i32);
    const auto sum = edit.append(*add_i32_schema, {*known_seven, input});
    edit.ret(mixed->entry(), {sum.result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto mixed_arguments = mixed->instructions().front().arguments();
  ok &= expect(mixed_arguments.size() == 2U && mixed_arguments.front().known() &&
                   mixed_arguments.front().get<std::int64_t>() == 7 &&
                   !mixed_arguments.back().known(),
               "one argument sequence carries Known and Residual Values");

  bool needs_explicit_result = false;
  try {
    auto edit = function->edit();
    edit.append(*source_schema);
  } catch (const std::invalid_argument& error) {
    needs_explicit_result =
        std::string_view(error.what()).find("cannot infer type variable 'T'") !=
        std::string_view::npos;
  }
  ok &= expect(needs_explicit_result && compiler.ok(),
               "an output-only type variable needs an explicit result type");

  std::optional<joggle::Instruction> inserted;
  {
    auto edit = function->edit();
    inserted = edit.insert(*add, *cast_schema, {function->arguments()[0]});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(inserted && function->instructions().front() == *inserted,
               "an edit inserts before an existing instruction");

  const joggle::Instruction original_add = *add;
  {
    auto edit = function->edit();
    add = edit.replace(*add, *add_schema);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(add && !original_add.valid() && add->arguments().size() == 2U,
               "replacement preserves arguments and invalidates the old handle");

  std::optional<joggle::Instruction> rolled_back;
  {
    auto edit = function->edit();
    rolled_back =
        edit.append(*add_schema, {function->arguments()[0]}, {*integer});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok(),
                 "a schema-invalid transaction is rejected");
  }
  ok &= expect(rolled_back && !rolled_back->valid(),
               "rollback invalidates handles created by the failed edit");

  std::optional<joggle::Instruction> first_cast;
  std::optional<joggle::Instruction> second_cast;
  {
    auto edit = function->edit();
    first_cast = edit.append(*cast_schema, {add->result(0)});
    second_cast = edit.append(*cast_schema, {first_cast->result(0)});
    edit.ret(function->entry(), {first_cast->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  {
    auto edit = function->edit();
    edit.replace(first_cast->result(0), add->result(0));
    edit.erase(*first_cast);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(first_cast && !first_cast->valid() && second_cast &&
                   second_cast->arguments().front() == add->result(0) &&
                   function->entry().terminator().returned().front() == add->result(0),
               "replace rewires instruction and boundary uses before erase");

  auto inconsistent_returns = compiler.function();
  if (!inconsistent_returns) {
    return EXIT_FAILURE;
  }
  {
    auto edit = inconsistent_returns->edit();
    const auto condition = edit.argument(*boolean);
    const auto integer_value = edit.argument(*integer);
    const auto other_value = edit.argument(*other);
    const auto left = edit.block();
    const auto right = edit.block();
    edit.branch(inconsistent_returns->entry(), condition, left, {}, right, {});
    edit.ret(left, {integer_value});
    edit.ret(right, {other_value});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok(),
                 "all returns of an anonymous function share one signature");
  }

  auto invalid = compiler.function();
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
                     invalid->arguments().empty() &&
                     invalid->instructions().empty(),
                 "a type-invalid edit rolls the complete transaction back");
  }

  auto branched = compiler.function();
  if (!branched) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Block> merge;
  {
    auto edit = branched->edit();
    const auto condition = edit.argument(*boolean);
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*integer);
    const auto left = edit.block();
    const auto right = edit.block();
    merge = edit.block({*integer});
    edit.branch(branched->entry(), condition, left, {}, right, {});
    edit.jump(left, *merge, {lhs});
    edit.jump(right, *merge, {rhs});
    edit.ret(*merge, {merge->arguments().front()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto entry_terminator = branched->entry().terminator();
  ok &= expect(compiler.verify(*branched) && branched->blocks().size() == 4U &&
                   merge && merge->arguments().front().is_block_argument() &&
                   entry_terminator.kind() ==
                       joggle::Terminator::Kind::Branch &&
                   entry_terminator.successor_count() == 2U &&
                   merge->terminator().returned().front() ==
                       merge->arguments().front(),
               "branches use sibling blocks and typed successor arguments");

  auto invalid_edge = compiler.function();
  if (!invalid_edge) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_edge->edit();
    const auto condition = edit.argument(*boolean);
    const auto wrong = edit.argument(*other);
    const auto target = edit.block({*integer});
    edit.jump(target, target, {target.arguments().front()});
    edit.branch(invalid_edge->entry(), condition, target, {wrong}, target,
                {wrong});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok() &&
                     invalid_edge->blocks().size() == 1U,
                 "edge type errors reject and roll back the whole CFG edit");
  }

  auto invalid_dominance = compiler.function();
  if (!invalid_dominance) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_dominance->edit();
    const auto condition = edit.argument(*boolean);
    const auto input = edit.argument(*integer);
    const auto left = edit.block();
    const auto right = edit.block();
    const auto merge_block = edit.block();
    edit.branch(invalid_dominance->entry(), condition, left, {}, right, {});
    const auto produced = edit.append(left, *cast_schema, {input});
    edit.jump(left, merge_block);
    edit.append(right, *cast_schema, {produced.result(0)});
    edit.jump(right, merge_block);
    edit.ret(merge_block);
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) && !diagnostics.ok() &&
                     invalid_dominance->blocks().size() == 1U,
                 "a sibling block cannot consume another sibling's result");
  }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
