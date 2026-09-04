#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

#include "transform/match.h"

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
  compiler.add(R"(
joggle 1;

mod mapping@1.0.0 {
  type word();
  type alternate();
  type memory();

  fn keep(input: word) -> word;
  fn converted(input: word) -> word;
  fn other(input: word) -> word;
  fn binary(lhs: word, rhs: word) -> word;
  fn identity<T>(input: T) -> T;
  fn apply(input: word, body: (word) -> word) -> word;
  fn advance(token: effect<memory>) -> effect<memory>;
  fn split(input: word) -> (word, word);
  fn configured(input: word, axis: int) -> word;
  fn callback_a(input: word) -> word;
  fn callback_b(input: word) -> word;

  fn first(input: word) -> word {
    return keep(input);
  }

  fn second(input: word) -> word {
    return other(input);
  }

  fn expanded(input: word) -> word {
    return keep(input);
  }

  fn with_inline(input: word) -> word {
    return apply(input, (value: word) => keep(value));
  }

  fn pair(input: word) -> (word, word) {
    return input, input;
  }

  fn effect_template(token: effect<memory>) -> effect<memory> {
    return advance(token);
  }

  fn multi_call_template(input: word) -> word {
    first, second = split(input);
    return first;
  }

  fn chain(input: word) -> word {
    return other(keep(input));
  }

  fn repeated(input: word) -> word {
    return binary(input, input);
  }

  fn different(lhs: word, rhs: word) -> word {
    return binary(lhs, rhs);
  }

  fn distinct_calls(input: word) -> word {
    return binary(keep(input), keep(input));
  }

  fn shared_call(input: word) -> word {
    value = keep(input);
    return binary(value, value);
  }

  fn axis_one(input: word) -> word {
    return configured(input, 1);
  }

  fn axis_two(input: word) -> word {
    return configured(input, 2);
  }

  fn reference_a(input: word) -> word {
    return apply(input, callback_a);
  }

  fn reference_b(input: word) -> word {
    return apply(input, callback_b);
  }

  fn unused_hole(input: word, unused: word) -> word {
    return keep(input);
  }

  fn replacement(input: word) -> word {
    return converted(input);
  }

  fn alternate_replacement(input: alternate) -> alternate {
    return identity(input);
  }

  fn double_keep(input: word) -> word {
    return keep(keep(input));
  }

  fn triple_keep(input: word) -> word {
    return keep(keep(keep(input)));
  }
}
)",
               "mapping.joggle");
  compiler.add(R"(
joggle 1;
mod foreign@1.0.0 {
  import mapping@1;
  fn external(input: mapping.word) -> mapping.word;
  fn replacement(input: mapping.word) -> mapping.word {
    return external(input);
  }
}
)",
               "foreign.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto schema = compiler.mod("mapping");
  const auto keep = schema ? schema->fn("keep") : std::nullopt;
  const auto converted = schema ? schema->fn("converted") : std::nullopt;
  const auto other = schema ? schema->fn("other") : std::nullopt;
  const auto binary = schema ? schema->fn("binary") : std::nullopt;
  const auto identity = schema ? schema->fn("identity") : std::nullopt;
  const auto word_type = schema ? schema->type("word") : std::nullopt;
  const auto alternate_type = schema ? schema->type("alternate") : std::nullopt;
  const auto foreign_mod = compiler.mod("foreign");
  const auto foreign_external =
      foreign_mod ? foreign_mod->fn("external") : std::nullopt;
  const auto word = word_type ? compiler.make(*word_type) : std::nullopt;
  const auto alternate =
      alternate_type ? compiler.make(*alternate_type) : std::nullopt;
  const auto i1 = compiler.make("i1");
  auto first = compiler.materialize("mapping.first");
  auto second = compiler.materialize("mapping.second");
  auto expanded = compiler.materialize("mapping.expanded");
  auto convertible = compiler.materialize("mapping.first");
  auto fixedpoint = compiler.materialize("mapping.expanded");
  auto oscillating = compiler.materialize("mapping.expanded");
  auto with_inline = compiler.materialize("mapping.with_inline");
  auto pair = compiler.materialize("mapping.pair");
  auto effect_template = compiler.materialize("mapping.effect_template");
  auto multi_call_template =
      compiler.materialize("mapping.multi_call_template");
  auto chain = compiler.materialize("mapping.chain");
  auto repeated = compiler.materialize("mapping.repeated");
  auto different = compiler.materialize("mapping.different");
  auto distinct_calls = compiler.materialize("mapping.distinct_calls");
  auto shared_call = compiler.materialize("mapping.shared_call");
  auto axis_one = compiler.materialize("mapping.axis_one");
  auto axis_two = compiler.materialize("mapping.axis_two");
  auto reference_a = compiler.materialize("mapping.reference_a");
  auto reference_b = compiler.materialize("mapping.reference_b");
  auto unused_hole = compiler.materialize("mapping.unused_hole");
  auto replacement = compiler.materialize("mapping.replacement");
  auto alternate_replacement =
      compiler.materialize("mapping.alternate_replacement");
  auto double_keep = compiler.materialize("mapping.double_keep");
  auto triple_keep = compiler.materialize("mapping.triple_keep");
  auto foreign_replacement = compiler.materialize("foreign.replacement");
  if (!keep || !converted || !other || !binary || !identity ||
      !foreign_external || !word || !alternate || !i1 || !first || !second ||
      !expanded || !convertible || !fixedpoint || !oscillating ||
      !with_inline || !pair || !effect_template || !multi_call_template ||
      !chain || !repeated || !different || !distinct_calls || !shared_call ||
      !axis_one || !axis_two || !reference_a || !reference_b || !unused_hole ||
      !replacement || !alternate_replacement || !double_keep || !triple_keep ||
      !foreign_replacement) {
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Diag valid_template_diagnostics;
  ok &= expect(joggle::detail::validate_expression_template(
                   *expanded, "before", valid_template_diagnostics) &&
                   valid_template_diagnostics.ok(),
               "one pure returned call is a valid expression template");

  joggle::Diag result_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *pair, "before", result_template_diagnostics) &&
                   !result_template_diagnostics.ok() &&
                   result_template_diagnostics.issues().front().message.find(
                       "exactly one result") != std::string::npos,
               "an expression template rejects multiple fn results");

  joggle::Diag effect_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *effect_template, "before", effect_template_diagnostics) &&
                   !effect_template_diagnostics.ok() &&
                   effect_template_diagnostics.issues().front().message.find(
                       "effect token") != std::string::npos,
               "the first expression matcher rejects effectful templates");

  joggle::Diag multi_call_template_diagnostics;
  ok &= expect(
      !joggle::detail::validate_expression_template(
          *multi_call_template, "before", multi_call_template_diagnostics) &&
          !multi_call_template_diagnostics.ok() &&
          multi_call_template_diagnostics.issues().front().message.find(
              "call with multiple results") != std::string::npos,
      "an expression DAG rejects calls whose result has tuple semantics");

  joggle::Diag inline_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *with_inline, "before", inline_template_diagnostics) &&
                   !inline_template_diagnostics.ok() &&
                   inline_template_diagnostics.issues().front().message.find(
                       "nested inline fn") != std::string::npos,
               "an expression template has no nested callable body");

  auto dead_template = compiler.create_fn();
  if (!dead_template) {
    return EXIT_FAILURE;
  }
  {
    auto edit = dead_template->edit();
    const auto input = edit.argument(*word);
    const auto root = edit.append(*keep, {input});
    static_cast<void>(edit.append(*other, {input}));
    edit.ret(dead_template->entry(), {root.value()});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diag dead_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *dead_template, "before", dead_template_diagnostics) &&
                   !dead_template_diagnostics.ok() &&
                   dead_template_diagnostics.issues().front().message.find(
                       "outside its returned expression") != std::string::npos,
               "a template is one rooted DAG and contains no dead call");

  joggle::Diag unused_hole_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *unused_hole, "before", unused_hole_diagnostics) &&
                   !unused_hole_diagnostics.ok() &&
                   unused_hole_diagnostics.issues().front().message.find(
                       "unused hole") != std::string::npos,
               "every declared template hole must be reachable from the root");

  joggle::Diag chain_match_diagnostics;
  const auto chain_matches = joggle::detail::match_expressions(
      *chain, *chain, chain_match_diagnostics);
  ok &= expect(chain_matches && chain_matches->size() == 1U &&
                   chain_match_diagnostics.ok() &&
                   chain_matches->front().bindings == chain->arguments() &&
                   chain_matches->front().calls == chain->ops() &&
                   chain_matches->front().root ==
                       chain->entry().terminator().returned().front(),
               "typed DAG matching binds holes and records calls in Fn "
               "order");

  auto two_chains = compiler.create_fn();
  if (!two_chains) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Val> first_chain_root;
  std::optional<joggle::Val> second_chain_root;
  {
    auto edit = two_chains->edit();
    const auto input = edit.argument(*word);
    const auto first_inner = edit.append(*keep, {input});
    first_chain_root = edit.append(*other, {first_inner.value()}).value();
    const auto second_inner = edit.append(*keep, {input});
    second_chain_root = edit.append(*other, {second_inner.value()}).value();
    edit.ret(two_chains->entry(), {*second_chain_root});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diag ordered_match_diagnostics;
  const auto ordered_matches = joggle::detail::match_expressions(
      *two_chains, *chain, ordered_match_diagnostics);
  ok &= expect(ordered_matches && ordered_matches->size() == 2U &&
                   ordered_match_diagnostics.ok() && first_chain_root &&
                   second_chain_root &&
                   (*ordered_matches)[0].root == *first_chain_root &&
                   (*ordered_matches)[1].root == *second_chain_root,
               "candidates are reported in committed Fn order");

  joggle::Diag repeated_match_diagnostics;
  const auto repeated_matches = joggle::detail::match_expressions(
      *repeated, *repeated, repeated_match_diagnostics);
  joggle::Diag unequal_match_diagnostics;
  const auto unequal_matches = joggle::detail::match_expressions(
      *different, *repeated, unequal_match_diagnostics);
  ok &= expect(repeated_matches && repeated_matches->size() == 1U &&
                   repeated_matches->front().bindings.size() == 1U &&
                   unequal_matches && unequal_matches->empty() &&
                   repeated_match_diagnostics.ok() &&
                   unequal_match_diagnostics.ok(),
               "a repeated hole is an SSA equality constraint");

  joggle::Diag injective_match_diagnostics;
  const auto injective_matches = joggle::detail::match_expressions(
      *shared_call, *distinct_calls, injective_match_diagnostics);
  ok &= expect(injective_matches && injective_matches->empty() &&
                   injective_match_diagnostics.ok(),
               "two distinct template calls cannot collapse onto one subject "
               "call");

  joggle::Diag known_match_diagnostics;
  const auto known_matches = joggle::detail::match_expressions(
      *axis_two, *axis_one, known_match_diagnostics);
  joggle::Diag reference_match_diagnostics;
  const auto reference_matches = joggle::detail::match_expressions(
      *reference_b, *reference_a, reference_match_diagnostics);
  ok &= expect(known_matches && known_matches->empty() && reference_matches &&
                   reference_matches->empty() && known_match_diagnostics.ok() &&
                   reference_match_diagnostics.ok(),
               "Known properties compare canonically and fn references "
               "compare by declaration identity");

  auto escaping = compiler.create_fn();
  if (!escaping) {
    return EXIT_FAILURE;
  }
  {
    auto edit = escaping->edit();
    const auto input = edit.argument(*word);
    const auto inner = edit.append(*keep, {input});
    const auto root = edit.append(*other, {inner.value()});
    static_cast<void>(edit.append(*binary, {inner.value(), input}));
    edit.ret(escaping->entry(), {root.value()});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diag closure_match_diagnostics;
  const auto closure_matches = joggle::detail::match_expressions(
      *escaping, *chain, closure_match_diagnostics);
  ok &= expect(closure_matches && closure_matches->size() == 1U &&
                   closure_match_diagnostics.ok(),
               "a pure expression may match through a shared DAG ancestor");
  joggle::Diag shared_replace_diagnostics;
  const auto shared_replaced = joggle::replace(*escaping, *chain, *replacement,
                                               shared_replace_diagnostics);
  const auto shared_ops = escaping->ops();
  ok &= expect(shared_replaced && *shared_replaced == 1U &&
                   shared_replace_diagnostics.ok() && shared_ops.size() == 3U &&
                   shared_ops[0].callee() == *keep &&
                   shared_ops[1].callee() == *converted &&
                   shared_ops[2].callee() == *binary &&
                   escaping->entry().terminator().returned().front() ==
                       shared_ops[1].value(),
               "replacement preserves a shared ancestor and its outside user");

  auto shared_branches = compiler.create_fn();
  if (!shared_branches) {
    return EXIT_FAILURE;
  }
  {
    auto edit = shared_branches->edit();
    const auto input = edit.argument(*word);
    const auto shared = edit.append(*keep, {input});
    const auto left = edit.append(*other, {shared.value()});
    const auto right = edit.append(*other, {shared.value()});
    const auto joined = edit.append(*binary, {left.value(), right.value()});
    edit.ret(shared_branches->entry(), {joined.value()});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diag first_branch_diagnostics;
  const auto first_branch = joggle::replace(
      *shared_branches, *chain, *replacement, first_branch_diagnostics);
  joggle::Diag second_branch_diagnostics;
  const auto second_branch = joggle::replace(
      *shared_branches, *chain, *replacement, second_branch_diagnostics);
  const auto branch_ops = shared_branches->ops();
  ok &= expect(first_branch && *first_branch == 1U && second_branch &&
                   *second_branch == 1U && first_branch_diagnostics.ok() &&
                   second_branch_diagnostics.ok() && branch_ops.size() == 3U &&
                   branch_ops[0].callee() == *converted &&
                   branch_ops[1].callee() == *converted &&
                   branch_ops[2].callee() == *binary,
               "repeated replacement consumes both branches sharing one "
               "ancestor");

  joggle::Fn replacement_subject = *chain;
  const auto replaced_root_location = replacement_subject.entry()
                                          .terminator()
                                          .returned()
                                          .front()
                                          .defining_op()
                                          ->location();
  joggle::Diag expression_replace_diagnostics;
  const auto expression_replaced =
      joggle::replace(replacement_subject, *chain, *replacement,
                      expression_replace_diagnostics);
  ok &=
      expect(expression_replaced && *expression_replaced == 1U &&
                 expression_replace_diagnostics.ok() &&
                 replaced_root_location.has_value() &&
                 replacement_subject.ops().size() == 1U &&
                 replacement_subject.ops().front().callee() == *converted &&
                 replacement_subject.ops().front().location() ==
                     replaced_root_location &&
                 replacement_subject.entry().terminator().returned().front() ==
                     replacement_subject.ops().front().value(),
             "replacement clones the after DAG and removes the matched DAG "
             "in one transaction");

  joggle::Fn no_match_subject = *axis_two;
  const auto no_match_revision = no_match_subject.revision();
  joggle::Diag expression_no_match_diagnostics;
  const auto expression_no_match =
      joggle::replace(no_match_subject, *axis_one, *replacement,
                      expression_no_match_diagnostics);
  ok &= expect(expression_no_match && *expression_no_match == 0U &&
                   expression_no_match_diagnostics.ok() &&
                   no_match_subject.revision() == no_match_revision,
               "a successful expression no-op preserves Fn revision");

  joggle::Fn incompatible_subject = *chain;
  const auto incompatible_revision = incompatible_subject.revision();
  joggle::Diag incompatible_replace_diagnostics;
  const auto incompatible_replace = joggle::replace(
      incompatible_subject, *chain, *pair, incompatible_replace_diagnostics);
  ok &=
      expect(!incompatible_replace && !incompatible_replace_diagnostics.ok() &&
                 incompatible_subject.revision() == incompatible_revision,
             "an incompatible after signature publishes no Fn edit");

  joggle::Fn wrong_type_subject = *chain;
  const auto wrong_type_revision = wrong_type_subject.revision();
  joggle::Diag wrong_type_replace_diagnostics;
  const auto wrong_type_replace =
      joggle::replace(wrong_type_subject, *chain, *alternate_replacement,
                      wrong_type_replace_diagnostics);
  ok &= expect(!wrong_type_replace && !wrong_type_replace_diagnostics.ok() &&
                   wrong_type_subject.revision() == wrong_type_revision,
               "replacement rejects a hole type mismatch without publishing");

  joggle::Fn overlap_subject = *triple_keep;
  joggle::Diag overlap_replace_diagnostics;
  const auto overlap_replace = joggle::replace(
      overlap_subject, *double_keep, *replacement, overlap_replace_diagnostics);
  ok &= expect(overlap_replace && *overlap_replace == 1U &&
                   overlap_replace_diagnostics.ok() &&
                   overlap_subject.ops().size() == 2U &&
                   overlap_subject.ops()[0].callee() == *converted &&
                   overlap_subject.ops()[1].callee() == *keep,
               "overlapping candidates select the first maximal "
               "non-overlapping match");

  joggle::Fn foreign_subject = *chain;
  joggle::Diag foreign_replace_diagnostics;
  const auto foreign_replace =
      joggle::replace(foreign_subject, *chain, *foreign_replacement,
                      foreign_replace_diagnostics);
  ok &= expect(foreign_replace && *foreign_replace == 1U &&
                   foreign_replace_diagnostics.ok() &&
                   foreign_subject.ops().size() == 1U &&
                   foreign_subject.ops().front().callee() == *foreign_external,
               "replacement extends the verified Fn mod closure for "
               "a newly cloned call");

  const std::string replacement_text =
      joggle::format(replacement_subject, "optimized");
  joggle::Compiler replacement_roundtrip;
  replacement_roundtrip.add(*schema);
  replacement_roundtrip.add("joggle 1;\nmod replacement_roundtrip@1.0.0 {\n"
                            "  import mapping@1;\n" +
                                replacement_text + "}\n",
                            "replacement-roundtrip.joggle");
  const bool replacement_roundtrip_linked = replacement_roundtrip.link();
  const auto replacement_roundtrip_fn =
      replacement_roundtrip_linked
          ? replacement_roundtrip.materialize("replacement_roundtrip.optimized")
          : std::nullopt;
  ok &= expect(replacement_roundtrip_fn &&
                   joggle::format(*replacement_roundtrip_fn, "optimized") ==
                       replacement_text,
               "a replaced Fn has canonical round-trippable source");

  joggle::Mod expression_mod("expression_mod", {1, 0, 0});
  joggle::Diag expression_mod_insert_diagnostics;
  if (!expression_mod.insert("first", joggle::Fn{*chain},
                             expression_mod_insert_diagnostics) ||
      !expression_mod.insert("second", joggle::Fn{*chain},
                             expression_mod_insert_diagnostics)) {
    expression_mod_insert_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  joggle::Diag expression_mod_diagnostics;
  const auto expression_mod_replaced = joggle::replace(
      expression_mod, *chain, *replacement, expression_mod_diagnostics);
  const auto expression_mod_first = expression_mod.fn("first");
  const auto expression_mod_second = expression_mod.fn("second");
  ok &= expect(
      expression_mod_replaced && *expression_mod_replaced == 2U &&
          expression_mod_diagnostics.ok() && expression_mod_first &&
          expression_mod_second && expression_mod_first->body() &&
          expression_mod_second->body() &&
          expression_mod_first->body()->ops().front().callee() == *converted &&
          expression_mod_second->body()->ops().front().callee() == *converted,
      "whole-Mod expression replacement publishes all changed members");

  const std::string expression_mod_digest(expression_mod.digest());
  joggle::Diag expression_mod_failure_diagnostics;
  const auto expression_mod_failure = joggle::replace(
      expression_mod, *chain, *pair, expression_mod_failure_diagnostics);
  ok &= expect(!expression_mod_failure &&
                   !expression_mod_failure_diagnostics.ok() &&
                   expression_mod.digest() == expression_mod_digest,
               "whole-Mod replacement failure publishes no partial value");

  joggle::Mod rollback_mod("rollback_mod", {1, 0, 0});
  joggle::Diag rollback_insert_diagnostics;
  if (!rollback_mod.insert("first", joggle::Fn{*chain},
                           rollback_insert_diagnostics) ||
      !rollback_mod.insert("second", joggle::Fn{*chain},
                           rollback_insert_diagnostics)) {
    rollback_insert_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rollback_second = rollback_mod.fn("second");
  const std::string rollback_digest(rollback_mod.digest());
  bool rollback_preserved = false;
  if (rollback_second) {
    joggle::Fn* busy = rollback_mod.body(*rollback_second);
    if (busy) {
      auto pending = busy->edit();
      joggle::Diag rollback_diagnostics;
      const auto rolled_back = joggle::replace(
          rollback_mod, *chain, *replacement, rollback_diagnostics);
      rollback_preserved = !rolled_back && !rollback_diagnostics.ok() &&
                           rollback_mod.digest() == rollback_digest;
    }
  }
  ok &= expect(rollback_preserved,
               "a later member failure publishes no earlier Mod "
               "replacement");

  const auto first_revision = first->revision();
  joggle::Diag no_op_diagnostics;
  const auto no_op = joggle::map_calls(
      *first,
      [](const joggle::Op&) -> std::optional<joggle::Mod::FnDecl> {
        return std::nullopt;
      },
      no_op_diagnostics);
  ok &= expect(no_op && *no_op == 0U && no_op_diagnostics.ok() &&
                   first->revision() == first_revision &&
                   first->ops().front().callee() == *keep,
               "a no-op mapping preserves the Fn revision");

  joggle::Diag no_op_rewrite_diagnostics;
  const auto no_op_rewrite = joggle::rewrite(
      *first,
      [](const joggle::Op&, joggle::Fn::Edit&, joggle::Diag&) { return false; },
      no_op_rewrite_diagnostics);
  ok &= expect(no_op_rewrite && *no_op_rewrite == 0U &&
                   no_op_rewrite_diagnostics.ok() &&
                   first->revision() == first_revision,
               "a no-op rewrite preserves the Fn revision");

  joggle::Diag replace_diagnostics;
  const auto replaced =
      joggle::replace_calls(*first, *keep, *converted, replace_diagnostics);
  ok &= expect(replaced && *replaced == 1U && replace_diagnostics.ok() &&
                   first->revision() != first_revision &&
                   first->ops().front().callee() == *converted,
               "a committed replacement advances the Fn revision");

  const auto expanded_revision = expanded->revision();
  joggle::Diag rewrite_diagnostics;
  const auto rewritten = joggle::rewrite(
      *expanded,
      [&](const joggle::Op& op, joggle::Fn::Edit& edit, joggle::Diag&) {
        if (op.callee() != *keep) {
          return false;
        }
        const auto first_step = edit.insert(op, *converted, op.arguments());
        const auto second_step = edit.insert(op, *other, {first_step.value()});
        edit.replace(op, {second_step.value()});
        return true;
      },
      rewrite_diagnostics);
  const auto expanded_ops = expanded->ops();
  ok &= expect(rewritten && *rewritten == 1U && rewrite_diagnostics.ok() &&
                   expanded->revision() != expanded_revision &&
                   expanded_ops.size() == 2U &&
                   expanded_ops[0].callee() == *converted &&
                   expanded_ops[1].callee() == *other &&
                   expanded->entry().terminator().returned().front() ==
                       expanded_ops[1].value(),
               "one lambda transactionally expands a call into multiple Ops");

  const auto convertible_revision = convertible->revision();
  joggle::Diag conversion_diagnostics;
  const auto conversion = joggle::convert(
      *convertible,
      [&](const joggle::Op& op, joggle::Fn::Edit& edit, joggle::Diag&) {
        if (op.callee() != *keep) {
          return false;
        }
        edit.replace(op, *converted);
        return true;
      },
      [&](const joggle::Op& op) { return op.callee() != *keep; },
      conversion_diagnostics);
  ok &= expect(conversion && *conversion == 1U && conversion_diagnostics.ok() &&
                   convertible->revision() != convertible_revision &&
                   convertible->ops().front().callee() == *converted,
               "conversion publishes a rewritten legal Fn");

  const auto staged_rewrite = [&](const joggle::Op& op, joggle::Fn::Edit& edit,
                                  joggle::Diag&) {
    if (op.callee() == *keep) {
      edit.replace(op, *converted);
      return true;
    }
    if (op.callee() == *converted) {
      edit.replace(op, *other);
      return true;
    }
    return false;
  };
  joggle::Diag fixedpoint_diagnostics;
  const auto fixedpoint_changes = joggle::rewrite_to_fixpoint(
      *fixedpoint, staged_rewrite, 3U, fixedpoint_diagnostics);
  ok &= expect(fixedpoint_changes && *fixedpoint_changes == 2U &&
                   fixedpoint_diagnostics.ok() &&
                   fixedpoint->ops().front().callee() == *other,
               "bounded sweeps process calls inserted by an earlier sweep");

  const auto oscillating_revision = oscillating->revision();
  joggle::Diag oscillating_diagnostics;
  const auto oscillating_result = joggle::rewrite_to_fixpoint(
      *oscillating,
      [&](const joggle::Op& op, joggle::Fn::Edit& edit, joggle::Diag&) {
        if (op.callee() == *keep) {
          edit.replace(op, *converted);
        } else {
          edit.replace(op, *keep);
        }
        return true;
      },
      2U, oscillating_diagnostics);
  ok &= expect(!oscillating_result && !oscillating_diagnostics.ok() &&
                   oscillating->revision() == oscillating_revision &&
                   oscillating->ops().front().callee() == *keep,
               "a non-convergent rewrite publishes no intermediate sweep");

  const std::string before_invalid = joggle::format(*second, "second");
  const auto before_invalid_revision = second->revision();
  joggle::Diag invalid_diagnostics;
  const auto invalid =
      joggle::replace_calls(*second, *other, *binary, invalid_diagnostics);
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   second->revision() == before_invalid_revision &&
                   joggle::format(*second, "second") == before_invalid,
               "an invalid replacement restores content and revision");

  auto mod_first = compiler.materialize("mapping.first");
  auto mod_second = compiler.materialize("mapping.second");
  joggle::Mod mod("mapping_result", {1, 0, 0});
  joggle::Diag insertion_diagnostics;
  if (!mod_first || !mod_second ||
      !mod.insert("first", std::move(*mod_first), insertion_diagnostics) ||
      !mod.insert("second", std::move(*mod_second), insertion_diagnostics)) {
    insertion_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto read_body = [&](std::string_view name) {
    const auto fn = static_cast<const joggle::Mod&>(mod).fn(name);
    return fn ? fn->body() : nullptr;
  };
  const auto* original_first = read_body("first");
  const auto* original_second = read_body("second");
  const auto original_first_revision = original_first->revision();
  const auto original_second_revision = original_second->revision();
  const std::string original_digest(mod.digest());
  const std::string original_declaration_digest(mod.declaration_digest());

  joggle::Mod fixedpoint_mod = mod;
  joggle::Diag fixedpoint_mod_diagnostics;
  const auto fixedpoint_mod_changes = joggle::rewrite_to_fixpoint(
      fixedpoint_mod, staged_rewrite, 3U, fixedpoint_mod_diagnostics);
  const auto fixedpoint_first = fixedpoint_mod.fn("first");
  ok &= expect(fixedpoint_mod_changes && *fixedpoint_mod_changes == 2U &&
                   fixedpoint_mod_diagnostics.ok() && fixedpoint_first &&
                   fixedpoint_first->body() != nullptr &&
                   fixedpoint_first->body()->ops().front().callee() == *other &&
                   mod.digest() == original_digest,
               "fixed-point Mod rewriting publishes one final value");

  joggle::Diag mod_no_op_diagnostics;
  const auto mod_no_op = joggle::map_calls(
      mod,
      [](const joggle::Op&) -> std::optional<joggle::Mod::FnDecl> {
        return std::nullopt;
      },
      mod_no_op_diagnostics);
  ok &= expect(mod_no_op && *mod_no_op == 0U && mod_no_op_diagnostics.ok() &&
                   mod.digest() == original_digest &&
                   read_body("first") == original_first &&
                   read_body("second") == original_second,
               "a no-op Mod mapping preserves shared Fn storage");

  joggle::Diag mod_failure_diagnostics;
  const auto mod_failure = joggle::convert(
      mod,
      [&](const joggle::Op& op, joggle::Fn::Edit& edit, joggle::Diag&) {
        if (op.callee() == *keep) {
          edit.replace(op, *converted);
          return true;
        }
        return false;
      },
      [&](const joggle::Op& op) { return op.callee() != *other; },
      mod_failure_diagnostics);
  const auto* unchanged_first = read_body("first");
  const auto* unchanged_second = read_body("second");
  ok &= expect(!mod_failure && !mod_failure_diagnostics.ok() &&
                   mod_failure_diagnostics.issues().front().message.find(
                       "fn 'second'") != std::string::npos &&
                   mod.digest() == original_digest &&
                   unchanged_first != nullptr && unchanged_second != nullptr &&
                   unchanged_first->ops().front().callee() == *keep &&
                   unchanged_second->ops().front().callee() == *other,
               "an illegal whole-Mod conversion publishes no partial edits");

  joggle::Diag mod_success_diagnostics;
  const auto mod_success =
      joggle::replace_calls(mod, *keep, *converted, mod_success_diagnostics);
  const auto* mapped_first = read_body("first");
  const auto* preserved_second = read_body("second");
  const auto mapped_declaration = mod.fn("first");
  const std::string mapped_declaration_digest =
      mapped_declaration
          ? std::string(mapped_declaration->symbol().declaration_digest())
          : std::string{};
  ok &= expect(mod_success && *mod_success == 1U &&
                   mod_success_diagnostics.ok() && mapped_first != nullptr &&
                   mod.digest() != original_digest &&
                   mod.declaration_digest() == original_declaration_digest &&
                   mapped_declaration_digest == mod.declaration_digest() &&
                   mapped_first->revision() != original_first_revision &&
                   mapped_first->ops().front().callee() == *converted &&
                   preserved_second != nullptr &&
                   preserved_second->revision() == original_second_revision,
               "whole-Mod replacement advances only changed revisions while "
               "preserving declaration identity");

  auto cfg = compiler.create_fn();
  if (!cfg) {
    return EXIT_FAILURE;
  }
  {
    auto edit = cfg->edit();
    const auto condition = edit.argument(*i1);
    const auto input = edit.argument(*word);
    const auto yes = edit.blk();
    const auto no = edit.blk();
    const auto merge = edit.blk({*word});
    const auto converted_value = edit.append(yes, *identity, {input});
    edit.branch(cfg->entry(), condition, yes, {}, no, {});
    edit.jump(yes, merge, {converted_value.value()});
    edit.jump(no, merge, {input});
    edit.ret(merge, {merge.arguments().front()});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diag cfg_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *cfg, "before", cfg_template_diagnostics) &&
                   !cfg_template_diagnostics.ok() &&
                   cfg_template_diagnostics.issues().front().message.find(
                       "one entry block") != std::string::npos,
               "an expression template rejects branches and merges");
  joggle::Diag clone_diagnostics;
  const auto cloned = joggle::clone(
      compiler, *cfg,
      [&](const joggle::Val& value) -> std::optional<joggle::Type> {
        return value.type() == *word
                   ? std::optional<joggle::Type>{*alternate}
                   : std::optional<joggle::Type>{value.type()};
      },
      clone_diagnostics);
  ok &= expect(
      cloned && clone_diagnostics.ok() && cloned->blks().size() == 4U &&
          cloned->arguments().size() == 2U &&
          cloned->arguments()[1].type() == *alternate &&
          cloned->ops().size() == 1U &&
          cloned->ops().front().callee() == *identity &&
          cloned->ops().front().value().type() == *alternate &&
          cloned->result_types() == std::vector<joggle::Type>{*alternate},
      "clone preserves arbitrary CFG while mapping types and generic Ops");

  joggle::Diag inline_clone_diagnostics;
  const auto inline_clone = joggle::clone(
      compiler, *with_inline,
      [](const joggle::Val& value) -> std::optional<joggle::Type> {
        return value.type();
      },
      inline_clone_diagnostics);
  const auto cloned_arguments = inline_clone && inline_clone->ops().size() == 1U
                                    ? inline_clone->ops().front().arguments()
                                    : std::vector<joggle::Val>{};
  const auto cloned_body = cloned_arguments.size() == 2U
                               ? cloned_arguments.back().inline_fn()
                               : std::optional<joggle::Fn>{};
  ok &= expect(inline_clone && inline_clone_diagnostics.ok() && cloned_body &&
                   with_inline->ops().front().location().has_value() &&
                   cloned_body->ops().size() == 1U &&
                   cloned_body->ops().front().callee() == *keep &&
                   inline_clone->ops().front().location() ==
                       with_inline->ops().front().location() &&
                   compiler.verify(*inline_clone),
               "clone preserves source provenance and verifies an inline "
               "callable body");

  joggle::Compiler staged_replace;
  staged_replace.add(R"(
joggle 1;
mod staged_replace@1.0.0 {
  type word();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;
  fn converted(input: word) -> word;
  fn replace(input: fn, before: fn, after: fn) -> fn;
  fn inspect(input: fn) -> int;

  fn subject(input: word) -> word {
    return other(keep(input));
  }

  fn optimize(input: word) -> word {
    optimized = @replace(
      (value: word) => other(keep(value)),
      (value: word) => other(keep(value)),
      (value: word) => converted(value)
    );
    count = @inspect(optimized);
    return converted(input);
  }
}
)",
                     "staged-replace.joggle");
  const bool staged_replace_linked = staged_replace.link();
  const auto staged_replace_mod = staged_replace.mod("staged_replace");
  const auto staged_replace_decl =
      staged_replace_mod ? staged_replace_mod->fn("replace") : std::nullopt;
  const auto staged_optimize_decl =
      staged_replace_mod ? staged_replace_mod->fn("optimize") : std::nullopt;
  const auto staged_inspect_decl =
      staged_replace_mod ? staged_replace_mod->fn("inspect") : std::nullopt;
  const auto staged_converted_decl =
      staged_replace_mod ? staged_replace_mod->fn("converted") : std::nullopt;
  if (staged_replace_decl) {
    staged_replace.bind(
        *staged_replace_decl,
        [](joggle::Fn input, const joggle::Fn& before, const joggle::Fn& after,
           joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
          const auto count = joggle::replace(input, before, after, diagnostics);
          return count ? std::optional<joggle::Fn>{std::move(input)}
                       : std::nullopt;
        });
  }
  std::size_t inspected_replacement_calls = 0;
  std::optional<joggle::Mod::FnDecl> inspected_replacement_callee;
  if (staged_inspect_decl) {
    staged_replace.bind(
        *staged_inspect_decl, [&](const joggle::Fn& fn) -> std::int64_t {
          inspected_replacement_calls = fn.ops().size();
          if (!fn.ops().empty()) {
            inspected_replacement_callee = fn.ops().front().callee();
          }
          return static_cast<std::int64_t>(fn.ops().size());
        });
  }
  const auto staged_optimized =
      staged_replace_linked && staged_optimize_decl && staged_replace_decl &&
              staged_inspect_decl
          ? staged_replace.materialize("staged_replace.optimize")
          : std::nullopt;
  if (!staged_optimized || !staged_replace.ok()) {
    staged_replace.diag().print(std::cerr);
  }
  ok &= expect(
      staged_replace_linked && staged_replace_decl && staged_optimize_decl &&
          staged_inspect_decl && staged_converted_decl && staged_optimized &&
          staged_replace.ok() && staged_optimized->ops().size() == 1U &&
          staged_optimized->ops().front().callee() == *staged_converted_decl &&
          inspected_replacement_calls == 1U &&
          inspected_replacement_callee == staged_converted_decl,
      "an ordinary source fn invokes typed-lambda replacement "
      "through explicit @ staging");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
