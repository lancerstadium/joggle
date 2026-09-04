#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
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
    module control@1.0.0 {
      type other();
      type memory();
      fn source<T>() -> T;
      fn add_i32(lhs: i32, rhs: i32) -> i32;
      fn configure(input: i32, axis: int = 1) -> i32;
      fn callback(input: i32) -> i32;
      fn apply(input: i32, body: (i32) -> i32) -> i32;
      fn advance(token: effect<memory>) -> effect<memory>;
    }
  )",
               "control.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto test_ir = compiler.module("test_ir");
  const auto control = compiler.module("control");
  const auto integer_schema = test_ir ? test_ir->type("integer") : std::nullopt;
  const auto add_schema = test_ir ? test_ir->function("+") : std::nullopt;
  const auto cast_schema = test_ir ? test_ir->function("cast") : std::nullopt;
  const auto source_schema =
      control ? control->function("source") : std::nullopt;
  const auto add_i32_schema =
      control ? control->function("add_i32") : std::nullopt;
  const auto configure_schema =
      control ? control->function("configure") : std::nullopt;
  const auto callback_schema =
      control ? control->function("callback") : std::nullopt;
  const auto apply_schema = control ? control->function("apply") : std::nullopt;
  const auto advance_schema =
      control ? control->function("advance") : std::nullopt;
  const auto prelude = compiler.module("prelude");
  const auto callable_schema =
      prelude ? prelude->type("callable") : std::nullopt;
  const auto effect_schema =
      prelude ? prelude->type("effect") : std::nullopt;
  const auto other_schema = control ? control->type("other") : std::nullopt;
  const auto memory_schema = control ? control->type("memory") : std::nullopt;
  if (!integer_schema || !add_schema || !cast_schema || !source_schema ||
      !add_i32_schema || !configure_schema || !callback_schema || !apply_schema ||
      !advance_schema || !callable_schema || !effect_schema || !other_schema ||
      !memory_schema) {
    return EXIT_FAILURE;
  }
  compiler.verify(*integer_schema, [](const joggle::Type&,
                                      joggle::Diagnostics&) { return true; });
  const auto integer = compiler.make(*integer_schema, std::int64_t{8});
  const auto other = compiler.make(*other_schema);
  const auto boolean = compiler.make("i1");
  const auto i32 = compiler.make("i32");
  const auto memory = compiler.make(*memory_schema);
  const auto memory_effect =
      memory ? compiler.make(*effect_schema, *memory) : std::nullopt;
  auto function = compiler.create_function();
  if (!integer || !other || !boolean || !i32 || !memory_effect || !function) {
    return EXIT_FAILURE;
  }

  std::optional<joggle::Op> add;
  const joggle::SourceRange imported_location{
      "model.onnx#node/add", {7, 1}, {7, 2}};
  {
    auto edit = function->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*integer);
    add = edit.append(*add_schema, {lhs, rhs});
    edit.locate(*add, imported_location);
    edit.append(*cast_schema, {add->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &=
      expect(compiler.verify(*function), "a committed function body verifies");
  ok &=
      expect(function->arguments().size() == 2U &&
                 function->ops().size() == 2U &&
                 function->ops() == function->ops(),
             "a Function owns one ordered op view across its blocks");
  ok &= expect(add && add->value().type() == *integer &&
                   !add->result(0).is_function_argument() &&
                   !add->result(0).is_block_argument() &&
                   add->location() ==
                       std::optional<joggle::SourceRange>{imported_location},
               "op results retain their inferred type and frontend source");

  auto copied = *function;
  const auto shared_revision = copied.revision();
  {
    auto edit = copied.edit();
    edit.erase(copied.ops().back());
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(shared_revision == function->revision() &&
                   copied.revision() != function->revision() &&
                   copied.ops().size() == 1U &&
                   function->ops().size() == 2U,
               "Function copies share a revision and detach on edit");

  const auto known_seven = compiler.known(*i32, std::int64_t{7});
  auto mixed = compiler.create_function();
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
  const auto mixed_arguments = mixed->ops().front().arguments();
  const auto mixed_operands = mixed->ops().front().operands();
  ok &=
      expect(mixed_arguments.size() == 2U && mixed_arguments.front().known() &&
                 mixed_arguments.front().get<std::int64_t>() == 7 &&
                 !mixed_arguments.back().known() &&
                 mixed_operands == mixed_arguments &&
                 mixed->ops().front().properties().empty(),
             "Known literals on value ports remain SSA operands");

  auto configured = compiler.create_function();
  if (!configured) {
    return EXIT_FAILURE;
  }
  {
    auto edit = configured->edit();
    const auto input = edit.argument(*i32);
    const auto value = edit.append(*configure_schema, {input});
    edit.ret(configured->entry(), {value.value()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto configured_op = configured->ops().front();
  const auto configured_properties = configured_op.properties();
  ok &= expect(configured_op.operands().size() == 1U &&
                   configured_op.operand("input") ==
                       std::optional<joggle::Value>{configured->arguments().front()} &&
                   !configured_op.operand("axis") &&
                   configured_properties.size() == 1U &&
                   configured_properties.front().first == "axis" &&
                   configured_op.property<std::int64_t>("axis") == 1 &&
                   !configured_op.property("input"),
               "compiler-domain inputs are named immutable Op properties");

  const auto callable =
      compiler.make(*callable_schema, std::vector<joggle::Type>{*i32},
                    std::vector<joggle::Type>{*i32});
  auto higher_order = compiler.create_function();
  if (!callable || !higher_order) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Value> callback;
  std::optional<joggle::Op> applied;
  {
    auto edit = higher_order->edit();
    const auto input = edit.argument(*i32);
    callback = edit.reference(*callback_schema, *callable);
    applied = edit.append(*apply_schema, {input, *callback});
    edit.ret(higher_order->entry(), {applied->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(callback && applied && callback->referenced_function() &&
                   callback->referenced_function()->symbol() ==
                       callback_schema->symbol() &&
                   higher_order->dominates(*callback, *applied) &&
                   higher_order->users(*callback) ==
                       std::vector<joggle::Op>{*applied},
               "a typed function reference is a globally dominating value");
  bool wrong_callable_rejected = false;
  const auto wrong_callable =
      compiler.make(*callable_schema, std::vector<joggle::Type>{*boolean},
                    std::vector<joggle::Type>{*i32});
  try {
    auto edit = higher_order->edit();
    if (wrong_callable) {
      static_cast<void>(edit.reference(*callback_schema, *wrong_callable));
    }
  } catch (const std::invalid_argument&) {
    wrong_callable_rejected = true;
  }
  ok &= expect(wrong_callable_rejected,
               "a function reference rejects a mismatched callable type");

  auto inline_body = compiler.create_function();
  if (!inline_body) {
    return EXIT_FAILURE;
  }
  {
    auto edit = inline_body->edit();
    const auto input = edit.argument(*i32);
    edit.ret(inline_body->entry(), {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  auto inline_higher_order = compiler.create_function();
  std::optional<joggle::Value> inline_callable;
  if (!inline_higher_order) {
    return EXIT_FAILURE;
  }
  {
    auto edit = inline_higher_order->edit();
    const auto input = edit.argument(*i32);
    inline_callable = edit.callable(*inline_body, *callable);
    const auto result = edit.append(*apply_schema, {input, *inline_callable});
    edit.ret(inline_higher_order->entry(), {result.result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto recovered_inline =
      inline_callable ? inline_callable->inline_function() : std::nullopt;
  ok &= expect(inline_callable && !inline_callable->referenced_function() &&
                   recovered_inline && !recovered_inline->declaration() &&
                   recovered_inline->arguments().size() == 1U &&
                   recovered_inline->result_types() ==
                       std::vector<joggle::Type>{*i32} &&
                   compiler.verify(*inline_higher_order),
               "a typed inline Function is a callable Value without a "
               "synthetic Module declaration");

  bool wrong_inline_callable_rejected = false;
  try {
    auto edit = inline_higher_order->edit();
    if (wrong_callable) {
      static_cast<void>(edit.callable(*inline_body, *wrong_callable));
    }
  } catch (const std::invalid_argument&) {
    wrong_inline_callable_rejected = true;
  }
  ok &= expect(wrong_inline_callable_rejected,
               "an inline Function rejects a mismatched callable type");

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

  std::optional<joggle::Op> inserted;
  {
    auto edit = function->edit();
    inserted = edit.insert(*add, *cast_schema, {function->arguments()[0]});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(inserted && function->ops().front() == *inserted,
               "an edit inserts before an existing op");

  const joggle::Op original_add = *add;
  {
    auto edit = function->edit();
    add = edit.replace(*add, *add_schema);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &=
      expect(add && !original_add.valid() && add->arguments().size() == 2U,
             "replacement preserves arguments and invalidates the old handle");

  bool missing_argument_rejected = false;
  try {
    auto edit = function->edit();
    edit.append(*add_schema, {function->arguments()[0]}, {*integer});
  } catch (const std::invalid_argument& error) {
    missing_argument_rejected =
        std::string_view(error.what()).find("missing argument 'rhs'") !=
        std::string_view::npos;
  }
  ok &= expect(missing_argument_rejected,
               "a missing call argument is rejected immediately");

  std::optional<joggle::Op> first_cast;
  std::optional<joggle::Op> second_cast;
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
                   function->entry().terminator().returned().front() ==
                       add->result(0),
               "replace rewires op and boundary uses before erase");

  auto queried = compiler.create_function();
  std::optional<joggle::Value> queried_input;
  std::optional<joggle::Op> queried_first;
  std::optional<joggle::Op> queried_second;
  if (!queried) {
    return EXIT_FAILURE;
  }
  {
    auto edit = queried->edit();
    queried_input = edit.argument(*i32);
    queried_first =
        edit.append(*add_i32_schema, {*queried_input, *queried_input});
    queried_second = edit.append(*add_i32_schema,
                                 {queried_first->result(0), *queried_input});
    edit.ret(queried->entry(), {queried_second->result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto input_users = queried->users(*queried_input);
  const auto first_users = queried->users(queried_first->result(0));
  ok &= expect(
      input_users.size() == 2U && input_users[0] == *queried_first &&
          input_users[1] == *queried_second && first_users.size() == 1U &&
          first_users.front() == *queried_second &&
          queried_input->users() == input_users &&
          queried_first->value().users() == first_users,
      "use queries return each consuming op once in function order");
  ok &= expect(queried->has_uses(queried_second->result(0)) &&
                   queried->users(queried_second->result(0)).empty(),
               "boundary uses count without pretending terminators are "
               "ops");
  ok &= expect(
      queried->dominates(*queried_input, *queried_first) &&
          queried->dominates(queried_first->result(0), *queried_second) &&
          !queried->dominates(queried_second->result(0), *queried_first),
      "value dominance follows arguments, blocks, and op order");

  auto inconsistent_returns = compiler.create_function();
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

  auto invalid = compiler.create_function();
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
                     invalid->ops().empty(),
                 "a type-invalid edit rolls the complete transaction back");
  }

  auto branched = compiler.create_function();
  if (!branched) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Value> branch_condition;
  std::optional<joggle::Value> branch_lhs;
  std::optional<joggle::Value> branch_rhs;
  std::optional<joggle::Block> left;
  std::optional<joggle::Block> right;
  std::optional<joggle::Block> merge;
  {
    auto edit = branched->edit();
    branch_condition = edit.argument(*boolean);
    branch_lhs = edit.argument(*integer);
    branch_rhs = edit.argument(*integer);
    left = edit.block();
    right = edit.block();
    merge = edit.block({*integer});
    edit.branch(branched->entry(), *branch_condition, *left, {}, *right, {});
    edit.jump(*left, *merge, {*branch_lhs});
    edit.jump(*right, *merge, {*branch_rhs});
    edit.ret(*merge, {merge->arguments().front()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const auto entry_terminator = branched->entry().terminator();
  ok &= expect(
      compiler.verify(*branched) && branched->blocks().size() == 4U && merge &&
          merge->arguments().front().is_block_argument() &&
          entry_terminator.kind() == joggle::Terminator::Kind::Branch &&
          entry_terminator.successor_count() == 2U &&
          merge->terminator().returned().front() == merge->arguments().front(),
      "branches use sibling blocks and typed successor arguments");
  const auto left_predecessors = branched->predecessors(*left);
  const auto merge_predecessors = branched->predecessors(*merge);
  ok &= expect(branched->predecessors(branched->entry()).empty() &&
                   left_predecessors.size() == 1U &&
                   left_predecessors.front() == branched->entry() &&
                   merge_predecessors.size() == 2U &&
                   merge_predecessors[0] == *left &&
                   merge_predecessors[1] == *right,
               "predecessors expose control edges in function order");
  ok &= expect(branched->dominates(branched->entry(), *merge) &&
                   branched->dominates(*left, *left) &&
                   !branched->dominates(*left, *merge),
               "block dominance is queried directly from a Function");
  ok &=
      expect(branched->has_uses(*branch_condition) &&
                 branched->users(*branch_condition).empty() &&
                 branched->has_uses(*branch_lhs) &&
                 branched->users(*branch_lhs).empty() &&
                 branched->has_uses(merge->arguments().front()),
             "branch, edge, and return operands are visible as boundary uses");

  auto effect_cfg = compiler.create_function();
  if (!effect_cfg) {
    return EXIT_FAILURE;
  }
  {
    auto edit = effect_cfg->edit();
    const auto condition = edit.argument(*boolean);
    const auto token = edit.argument(*memory_effect);
    const auto yes = edit.block({*memory_effect});
    const auto no = edit.block({*memory_effect});
    const auto merge_effect = edit.block({*memory_effect});
    edit.branch(effect_cfg->entry(), condition, yes, {token}, no, {token});
    const auto yes_next =
        edit.append(yes, *advance_schema, {yes.arguments().front()});
    const auto no_next =
        edit.append(no, *advance_schema, {no.arguments().front()});
    edit.jump(yes, merge_effect, {yes_next.result(0)});
    edit.jump(no, merge_effect, {no_next.result(0)});
    edit.ret(merge_effect, {merge_effect.arguments().front()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  ok &= expect(compiler.verify(*effect_cfg) &&
                   effect_cfg->blocks().size() == 4U,
               "effect tokens may fork across exclusive branch edges and "
               "merge through a typed block argument");

  auto duplicated_effect = compiler.create_function();
  if (!duplicated_effect) {
    return EXIT_FAILURE;
  }
  {
    auto edit = duplicated_effect->edit();
    const auto token = edit.argument(*memory_effect);
    const auto first = edit.append(*advance_schema, {token});
    static_cast<void>(edit.append(*advance_schema, {token}));
    edit.ret(duplicated_effect->entry(), {first.result(0)});
    joggle::Diagnostics diagnostics;
    const bool committed = edit.commit(diagnostics);
    const bool reports_multiple_use = std::any_of(
        diagnostics.entries().begin(), diagnostics.entries().end(),
        [](const joggle::Diagnostic& diagnostic) {
          return diagnostic.message.find("more than one consuming use") !=
                 std::string::npos;
        });
    ok &= expect(!committed && reports_multiple_use &&
                     duplicated_effect->ops().empty(),
                 "one effect token cannot feed two calls on the same path");
  }

  auto duplicated_branch_effect = compiler.create_function();
  if (!duplicated_branch_effect) {
    return EXIT_FAILURE;
  }
  {
    auto edit = duplicated_branch_effect->edit();
    const auto condition = edit.argument(*boolean);
    const auto token = edit.argument(*memory_effect);
    const auto yes = edit.block({*memory_effect, *memory_effect});
    const auto no = edit.block({*memory_effect, *memory_effect});
    edit.branch(duplicated_branch_effect->entry(), condition, yes,
                {token, token}, no, {token, token});
    edit.ret(yes, {yes.arguments().front()});
    edit.ret(no, {no.arguments().front()});
    joggle::Diagnostics diagnostics;
    const bool committed = edit.commit(diagnostics);
    const bool reports_repeated_branch = std::any_of(
        diagnostics.entries().begin(), diagnostics.entries().end(),
        [](const joggle::Diagnostic& diagnostic) {
          return diagnostic.message.find("branch path repeats") !=
                 std::string::npos;
        });
    ok &= expect(!committed && reports_repeated_branch &&
                     duplicated_branch_effect->blocks().size() == 1U,
                 "one branch path cannot duplicate an effect token");
  }

  auto invalid_edge = compiler.create_function();
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

  auto invalid_dominance = compiler.create_function();
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
