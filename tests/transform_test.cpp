#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

#include "transform_internal.h"

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

module mapping@1.0.0 {
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
module foreign@1.0.0 {
  import mapping@1;
  fn external(input: mapping.word) -> mapping.word;
  fn replacement(input: mapping.word) -> mapping.word {
    return external(input);
  }
}
)",
               "foreign.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto schema = compiler.module("mapping");
  const auto keep = schema ? schema->function("keep") : std::nullopt;
  const auto converted = schema ? schema->function("converted") : std::nullopt;
  const auto other = schema ? schema->function("other") : std::nullopt;
  const auto binary = schema ? schema->function("binary") : std::nullopt;
  const auto identity = schema ? schema->function("identity") : std::nullopt;
  const auto word_type = schema ? schema->type("word") : std::nullopt;
  const auto alternate_type = schema ? schema->type("alternate") : std::nullopt;
  const auto foreign_module = compiler.module("foreign");
  const auto foreign_external =
      foreign_module ? foreign_module->function("external") : std::nullopt;
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
      !foreign_external || !word ||
      !alternate || !i1 || !first || !second || !expanded || !convertible ||
      !fixedpoint || !oscillating || !with_inline || !pair ||
      !effect_template || !multi_call_template || !chain || !repeated ||
      !different || !distinct_calls || !shared_call || !axis_one || !axis_two ||
      !reference_a || !reference_b || !unused_hole || !replacement ||
      !alternate_replacement || !double_keep || !triple_keep ||
      !foreign_replacement) {
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Diagnostics valid_template_diagnostics;
  ok &= expect(joggle::detail::validate_expression_template(
                   *expanded, "before", valid_template_diagnostics) &&
                   valid_template_diagnostics.ok(),
               "one pure returned call is a valid expression template");

  joggle::Diagnostics result_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *pair, "before", result_template_diagnostics) &&
                   !result_template_diagnostics.ok() &&
                   result_template_diagnostics.entries().front().message.find(
                       "exactly one result") != std::string::npos,
               "an expression template rejects multiple function results");

  joggle::Diagnostics effect_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *effect_template, "before", effect_template_diagnostics) &&
                   !effect_template_diagnostics.ok() &&
                   effect_template_diagnostics.entries().front().message.find(
                       "effect token") != std::string::npos,
               "the first expression matcher rejects effectful templates");

  joggle::Diagnostics multi_call_template_diagnostics;
  ok &= expect(
      !joggle::detail::validate_expression_template(
          *multi_call_template, "before", multi_call_template_diagnostics) &&
          !multi_call_template_diagnostics.ok() &&
          multi_call_template_diagnostics.entries().front().message.find(
              "call with multiple results") != std::string::npos,
      "an expression DAG rejects calls whose result has tuple semantics");

  joggle::Diagnostics inline_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *with_inline, "before", inline_template_diagnostics) &&
                   !inline_template_diagnostics.ok() &&
                   inline_template_diagnostics.entries().front().message.find(
                       "nested inline function") != std::string::npos,
               "an expression template has no nested callable body");

  auto dead_template = compiler.create_function();
  if (!dead_template) {
    return EXIT_FAILURE;
  }
  {
    auto edit = dead_template->edit();
    const auto input = edit.argument(*word);
    const auto root = edit.append(*keep, {input});
    static_cast<void>(edit.append(*other, {input}));
    edit.ret(dead_template->entry(), {root.value()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diagnostics dead_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *dead_template, "before", dead_template_diagnostics) &&
                   !dead_template_diagnostics.ok() &&
                   dead_template_diagnostics.entries().front().message.find(
                       "outside its returned expression") != std::string::npos,
               "a template is one rooted DAG and contains no dead call");

  joggle::Diagnostics unused_hole_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *unused_hole, "before", unused_hole_diagnostics) &&
                   !unused_hole_diagnostics.ok() &&
                   unused_hole_diagnostics.entries().front().message.find(
                       "unused hole") != std::string::npos,
               "every declared template hole must be reachable from the root");

  joggle::Diagnostics chain_match_diagnostics;
  const auto chain_matches = joggle::detail::match_expressions(
      *chain, *chain, chain_match_diagnostics);
  ok &= expect(chain_matches && chain_matches->size() == 1U &&
                   chain_match_diagnostics.ok() &&
                   chain_matches->front().bindings == chain->arguments() &&
                   chain_matches->front().calls == chain->ops() &&
                   chain_matches->front().root ==
                       chain->entry().terminator().returned().front(),
               "typed DAG matching binds holes and records calls in Function "
               "order");

  auto two_chains = compiler.create_function();
  if (!two_chains) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Value> first_chain_root;
  std::optional<joggle::Value> second_chain_root;
  {
    auto edit = two_chains->edit();
    const auto input = edit.argument(*word);
    const auto first_inner = edit.append(*keep, {input});
    first_chain_root = edit.append(*other, {first_inner.value()}).value();
    const auto second_inner = edit.append(*keep, {input});
    second_chain_root = edit.append(*other, {second_inner.value()}).value();
    edit.ret(two_chains->entry(), {*second_chain_root});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diagnostics ordered_match_diagnostics;
  const auto ordered_matches = joggle::detail::match_expressions(
      *two_chains, *chain, ordered_match_diagnostics);
  ok &= expect(ordered_matches && ordered_matches->size() == 2U &&
                   ordered_match_diagnostics.ok() && first_chain_root &&
                   second_chain_root &&
                   (*ordered_matches)[0].root == *first_chain_root &&
                   (*ordered_matches)[1].root == *second_chain_root,
               "candidates are reported in committed Function order");

  joggle::Diagnostics repeated_match_diagnostics;
  const auto repeated_matches = joggle::detail::match_expressions(
      *repeated, *repeated, repeated_match_diagnostics);
  joggle::Diagnostics unequal_match_diagnostics;
  const auto unequal_matches = joggle::detail::match_expressions(
      *different, *repeated, unequal_match_diagnostics);
  ok &= expect(repeated_matches && repeated_matches->size() == 1U &&
                   repeated_matches->front().bindings.size() == 1U &&
                   unequal_matches && unequal_matches->empty() &&
                   repeated_match_diagnostics.ok() &&
                   unequal_match_diagnostics.ok(),
               "a repeated hole is an SSA equality constraint");

  joggle::Diagnostics injective_match_diagnostics;
  const auto injective_matches = joggle::detail::match_expressions(
      *shared_call, *distinct_calls, injective_match_diagnostics);
  ok &= expect(injective_matches && injective_matches->empty() &&
                   injective_match_diagnostics.ok(),
               "two distinct template calls cannot collapse onto one subject "
               "call");

  joggle::Diagnostics known_match_diagnostics;
  const auto known_matches = joggle::detail::match_expressions(
      *axis_two, *axis_one, known_match_diagnostics);
  joggle::Diagnostics reference_match_diagnostics;
  const auto reference_matches = joggle::detail::match_expressions(
      *reference_b, *reference_a, reference_match_diagnostics);
  ok &= expect(known_matches && known_matches->empty() &&
                   reference_matches && reference_matches->empty() &&
                   known_match_diagnostics.ok() &&
                   reference_match_diagnostics.ok(),
               "Known properties compare canonically and function references "
               "compare by declaration identity");

  auto escaping = compiler.create_function();
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
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diagnostics closure_match_diagnostics;
  const auto closure_matches = joggle::detail::match_expressions(
      *escaping, *chain, closure_match_diagnostics);
  ok &= expect(closure_matches && closure_matches->empty() &&
                   closure_match_diagnostics.ok(),
               "an internal match result cannot escape to an unmatched call");

  joggle::Function replacement_subject = *chain;
  const auto replaced_root_location =
      replacement_subject.entry().terminator().returned().front().defining_op()
          ->location();
  joggle::Diagnostics expression_replace_diagnostics;
  const auto expression_replaced = joggle::replace(
      replacement_subject, *chain, *replacement,
      expression_replace_diagnostics);
  ok &= expect(expression_replaced && *expression_replaced == 1U &&
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

  joggle::Function no_match_subject = *axis_two;
  const auto no_match_revision = no_match_subject.revision();
  joggle::Diagnostics expression_no_match_diagnostics;
  const auto expression_no_match = joggle::replace(
      no_match_subject, *axis_one, *replacement,
      expression_no_match_diagnostics);
  ok &= expect(expression_no_match && *expression_no_match == 0U &&
                   expression_no_match_diagnostics.ok() &&
                   no_match_subject.revision() == no_match_revision,
               "a successful expression no-op preserves Function revision");

  joggle::Function incompatible_subject = *chain;
  const auto incompatible_revision = incompatible_subject.revision();
  joggle::Diagnostics incompatible_replace_diagnostics;
  const auto incompatible_replace = joggle::replace(
      incompatible_subject, *chain, *pair, incompatible_replace_diagnostics);
  ok &= expect(!incompatible_replace &&
                   !incompatible_replace_diagnostics.ok() &&
                   incompatible_subject.revision() == incompatible_revision,
               "an incompatible after signature publishes no Function edit");

  joggle::Function wrong_type_subject = *chain;
  const auto wrong_type_revision = wrong_type_subject.revision();
  joggle::Diagnostics wrong_type_replace_diagnostics;
  const auto wrong_type_replace =
      joggle::replace(wrong_type_subject, *chain, *alternate_replacement,
                      wrong_type_replace_diagnostics);
  ok &= expect(!wrong_type_replace && !wrong_type_replace_diagnostics.ok() &&
                   wrong_type_subject.revision() == wrong_type_revision,
               "replacement rejects a hole type mismatch without publishing");

  joggle::Function overlap_subject = *triple_keep;
  joggle::Diagnostics overlap_replace_diagnostics;
  const auto overlap_replace = joggle::replace(
      overlap_subject, *double_keep, *replacement,
      overlap_replace_diagnostics);
  ok &= expect(overlap_replace && *overlap_replace == 1U &&
                   overlap_replace_diagnostics.ok() &&
                   overlap_subject.ops().size() == 2U &&
                   overlap_subject.ops()[0].callee() == *converted &&
                   overlap_subject.ops()[1].callee() == *keep,
               "overlapping candidates select the first maximal "
               "non-overlapping match");

  joggle::Function foreign_subject = *chain;
  joggle::Diagnostics foreign_replace_diagnostics;
  const auto foreign_replace = joggle::replace(
      foreign_subject, *chain, *foreign_replacement,
      foreign_replace_diagnostics);
  ok &= expect(foreign_replace && *foreign_replace == 1U &&
                   foreign_replace_diagnostics.ok() &&
                   foreign_subject.ops().size() == 1U &&
                   foreign_subject.ops().front().callee() == *foreign_external,
               "replacement extends the verified Function module closure for "
               "a newly cloned call");

  const std::string replacement_text =
      joggle::format(replacement_subject, "optimized");
  joggle::Compiler replacement_roundtrip;
  replacement_roundtrip.add(*schema);
  replacement_roundtrip.add("joggle 1;\nmodule replacement_roundtrip@1.0.0 {\n"
                            "  import mapping@1;\n" +
                                replacement_text + "}\n",
                            "replacement-roundtrip.joggle");
  const bool replacement_roundtrip_linked = replacement_roundtrip.link();
  const auto replacement_roundtrip_function =
      replacement_roundtrip_linked
          ? replacement_roundtrip.materialize(
                "replacement_roundtrip.optimized")
          : std::nullopt;
  ok &= expect(replacement_roundtrip_function &&
                   joggle::format(*replacement_roundtrip_function,
                                  "optimized") == replacement_text,
               "a replaced Function has canonical round-trippable source");

  joggle::Module expression_module("expression_module", {1, 0, 0});
  joggle::Diagnostics expression_module_insert_diagnostics;
  if (!expression_module.insert("first", joggle::Function{*chain},
                                expression_module_insert_diagnostics) ||
      !expression_module.insert("second", joggle::Function{*chain},
                                expression_module_insert_diagnostics)) {
    expression_module_insert_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  joggle::Diagnostics expression_module_diagnostics;
  const auto expression_module_replaced = joggle::replace(
      expression_module, *chain, *replacement, expression_module_diagnostics);
  const auto expression_module_first = expression_module.function("first");
  const auto expression_module_second = expression_module.function("second");
  ok &= expect(
      expression_module_replaced && *expression_module_replaced == 2U &&
          expression_module_diagnostics.ok() && expression_module_first &&
          expression_module_second && expression_module_first->body() &&
          expression_module_second->body() &&
          expression_module_first->body()->ops().front().callee() ==
              *converted &&
          expression_module_second->body()->ops().front().callee() ==
              *converted,
      "whole-Module expression replacement publishes all changed members");

  const std::string expression_module_digest(expression_module.digest());
  joggle::Diagnostics expression_module_failure_diagnostics;
  const auto expression_module_failure = joggle::replace(
      expression_module, *chain, *pair,
      expression_module_failure_diagnostics);
  ok &= expect(!expression_module_failure &&
                   !expression_module_failure_diagnostics.ok() &&
                   expression_module.digest() == expression_module_digest,
               "whole-Module replacement failure publishes no partial value");

  joggle::Module rollback_module("rollback_module", {1, 0, 0});
  joggle::Diagnostics rollback_insert_diagnostics;
  if (!rollback_module.insert("first", joggle::Function{*chain},
                              rollback_insert_diagnostics) ||
      !rollback_module.insert("second", joggle::Function{*chain},
                              rollback_insert_diagnostics)) {
    rollback_insert_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rollback_second = rollback_module.function("second");
  const std::string rollback_digest(rollback_module.digest());
  bool rollback_preserved = false;
  if (rollback_second) {
    joggle::Function* busy = rollback_module.body(*rollback_second);
    if (busy) {
      auto pending = busy->edit();
      joggle::Diagnostics rollback_diagnostics;
      const auto rolled_back = joggle::replace(
          rollback_module, *chain, *replacement, rollback_diagnostics);
      rollback_preserved =
          !rolled_back && !rollback_diagnostics.ok() &&
          rollback_module.digest() == rollback_digest;
    }
  }
  ok &= expect(rollback_preserved,
               "a later member failure publishes no earlier Module "
               "replacement");

  const auto first_revision = first->revision();
  joggle::Diagnostics no_op_diagnostics;
  const auto no_op = joggle::map_calls(
      *first,
      [](const joggle::Op&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      no_op_diagnostics);
  ok &= expect(no_op && *no_op == 0U && no_op_diagnostics.ok() &&
                   first->revision() == first_revision &&
                   first->ops().front().callee() == *keep,
               "a no-op mapping preserves the Function revision");

  joggle::Diagnostics no_op_rewrite_diagnostics;
  const auto no_op_rewrite = joggle::rewrite(
      *first,
      [](const joggle::Op&, joggle::Function::Edit&,
         joggle::Diagnostics&) { return false; },
      no_op_rewrite_diagnostics);
  ok &= expect(no_op_rewrite && *no_op_rewrite == 0U &&
                   no_op_rewrite_diagnostics.ok() &&
                   first->revision() == first_revision,
               "a no-op rewrite preserves the Function revision");

  joggle::Diagnostics replace_diagnostics;
  const auto replaced =
      joggle::replace_calls(*first, *keep, *converted, replace_diagnostics);
  ok &= expect(replaced && *replaced == 1U && replace_diagnostics.ok() &&
                   first->revision() != first_revision &&
                   first->ops().front().callee() == *converted,
               "a committed replacement advances the Function revision");

  const auto expanded_revision = expanded->revision();
  joggle::Diagnostics rewrite_diagnostics;
  const auto rewritten = joggle::rewrite(
      *expanded,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (op.callee() != *keep) {
          return false;
        }
        const auto first_step =
            edit.insert(op, *converted, op.arguments());
        const auto second_step =
            edit.insert(op, *other, {first_step.value()});
        edit.replace(op, {second_step.value()});
        return true;
      },
      rewrite_diagnostics);
  const auto expanded_ops = expanded->ops();
  ok &= expect(
      rewritten && *rewritten == 1U && rewrite_diagnostics.ok() &&
          expanded->revision() != expanded_revision &&
          expanded_ops.size() == 2U &&
          expanded_ops[0].callee() == *converted &&
          expanded_ops[1].callee() == *other &&
          expanded->entry().terminator().returned().front() ==
              expanded_ops[1].value(),
      "one lambda transactionally expands a call into multiple Ops");

  const auto convertible_revision = convertible->revision();
  joggle::Diagnostics conversion_diagnostics;
  const auto conversion = joggle::convert(
      *convertible,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (op.callee() != *keep) {
          return false;
        }
        edit.replace(op, *converted);
        return true;
      },
      [&](const joggle::Op& op) {
        return op.callee() != *keep;
      },
      conversion_diagnostics);
  ok &= expect(conversion && *conversion == 1U && conversion_diagnostics.ok() &&
                   convertible->revision() != convertible_revision &&
                   convertible->ops().front().callee() == *converted,
               "conversion publishes a rewritten legal Function");

  const auto staged_rewrite = [&](const joggle::Op& op,
                                  joggle::Function::Edit& edit,
                                  joggle::Diagnostics&) {
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
  joggle::Diagnostics fixedpoint_diagnostics;
  const auto fixedpoint_changes = joggle::rewrite_to_fixpoint(
      *fixedpoint, staged_rewrite, 3U, fixedpoint_diagnostics);
  ok &= expect(fixedpoint_changes && *fixedpoint_changes == 2U &&
                   fixedpoint_diagnostics.ok() &&
                   fixedpoint->ops().front().callee() == *other,
               "bounded sweeps process calls inserted by an earlier sweep");

  const auto oscillating_revision = oscillating->revision();
  joggle::Diagnostics oscillating_diagnostics;
  const auto oscillating_result = joggle::rewrite_to_fixpoint(
      *oscillating,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
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
  joggle::Diagnostics invalid_diagnostics;
  const auto invalid =
      joggle::replace_calls(*second, *other, *binary, invalid_diagnostics);
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   second->revision() == before_invalid_revision &&
                   joggle::format(*second, "second") == before_invalid,
               "an invalid replacement restores content and revision");

  auto module_first = compiler.materialize("mapping.first");
  auto module_second = compiler.materialize("mapping.second");
  joggle::Module module("mapping_result", {1, 0, 0});
  joggle::Diagnostics insertion_diagnostics;
  if (!module_first || !module_second ||
      !module.insert("first", std::move(*module_first),
                     insertion_diagnostics) ||
      !module.insert("second", std::move(*module_second),
                     insertion_diagnostics)) {
    insertion_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto read_body = [&](std::string_view name) {
    const auto function =
        static_cast<const joggle::Module&>(module).function(name);
    return function ? function->body() : nullptr;
  };
  const auto* original_first = read_body("first");
  const auto* original_second = read_body("second");
  const auto original_first_revision = original_first->revision();
  const auto original_second_revision = original_second->revision();
  const std::string original_digest(module.digest());
  const std::string original_declaration_digest(module.declaration_digest());

  joggle::Module fixedpoint_module = module;
  joggle::Diagnostics fixedpoint_module_diagnostics;
  const auto fixedpoint_module_changes = joggle::rewrite_to_fixpoint(
      fixedpoint_module, staged_rewrite, 3U, fixedpoint_module_diagnostics);
  const auto fixedpoint_first = fixedpoint_module.function("first");
  ok &= expect(fixedpoint_module_changes && *fixedpoint_module_changes == 2U &&
                   fixedpoint_module_diagnostics.ok() && fixedpoint_first &&
                   fixedpoint_first->body() != nullptr &&
                   fixedpoint_first->body()->ops().front().callee() ==
                       *other &&
                   module.digest() == original_digest,
               "fixed-point Module rewriting publishes one final value");

  joggle::Diagnostics module_no_op_diagnostics;
  const auto module_no_op = joggle::map_calls(
      module,
      [](const joggle::Op&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      module_no_op_diagnostics);
  ok &= expect(module_no_op && *module_no_op == 0U &&
                   module_no_op_diagnostics.ok() &&
                   module.digest() == original_digest &&
                   read_body("first") == original_first &&
                   read_body("second") == original_second,
               "a no-op Module mapping preserves shared Function storage");

  joggle::Diagnostics module_failure_diagnostics;
  const auto module_failure = joggle::convert(
      module,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (op.callee() == *keep) {
          edit.replace(op, *converted);
          return true;
        }
        return false;
      },
      [&](const joggle::Op& op) {
        return op.callee() != *other;
      },
      module_failure_diagnostics);
  const auto* unchanged_first = read_body("first");
  const auto* unchanged_second = read_body("second");
  ok &= expect(!module_failure && !module_failure_diagnostics.ok() &&
                   module_failure_diagnostics.entries().front().message.find(
                       "function 'second'") != std::string::npos &&
                   module.digest() == original_digest &&
                   unchanged_first != nullptr && unchanged_second != nullptr &&
                   unchanged_first->ops().front().callee() == *keep &&
                   unchanged_second->ops().front().callee() == *other,
               "an illegal whole-Module conversion publishes no partial edits");

  joggle::Diagnostics module_success_diagnostics;
  const auto module_success = joggle::replace_calls(module, *keep, *converted,
                                                    module_success_diagnostics);
  const auto* mapped_first = read_body("first");
  const auto* preserved_second = read_body("second");
  const auto mapped_declaration = module.function("first");
  const std::string mapped_declaration_digest =
      mapped_declaration
          ? std::string(mapped_declaration->symbol().declaration_digest())
          : std::string{};
  ok &=
      expect(module_success && *module_success == 1U &&
                 module_success_diagnostics.ok() && mapped_first != nullptr &&
                 module.digest() != original_digest &&
                 module.declaration_digest() == original_declaration_digest &&
                 mapped_declaration_digest == module.declaration_digest() &&
                 mapped_first->revision() != original_first_revision &&
                 mapped_first->ops().front().callee() == *converted &&
                 preserved_second != nullptr &&
                 preserved_second->revision() == original_second_revision,
             "whole-Module replacement advances only changed revisions while "
             "preserving declaration identity");

  auto cfg = compiler.create_function();
  if (!cfg) {
    return EXIT_FAILURE;
  }
  {
    auto edit = cfg->edit();
    const auto condition = edit.argument(*i1);
    const auto input = edit.argument(*word);
    const auto yes = edit.block();
    const auto no = edit.block();
    const auto merge = edit.block({*word});
    const auto converted_value = edit.append(yes, *identity, {input});
    edit.branch(cfg->entry(), condition, yes, {}, no, {});
    edit.jump(yes, merge, {converted_value.value()});
    edit.jump(no, merge, {input});
    edit.ret(merge, {merge.arguments().front()});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  joggle::Diagnostics cfg_template_diagnostics;
  ok &= expect(!joggle::detail::validate_expression_template(
                   *cfg, "before", cfg_template_diagnostics) &&
                   !cfg_template_diagnostics.ok() &&
                   cfg_template_diagnostics.entries().front().message.find(
                       "one entry block") != std::string::npos,
               "an expression template rejects branches and merges");
  joggle::Diagnostics clone_diagnostics;
  const auto cloned = joggle::clone(
      compiler, *cfg,
      [&](const joggle::Value& value) -> std::optional<joggle::Type> {
        return value.type() == *word
                   ? std::optional<joggle::Type>{*alternate}
                   : std::optional<joggle::Type>{value.type()};
      },
      clone_diagnostics);
  ok &= expect(
      cloned && clone_diagnostics.ok() && cloned->blocks().size() == 4U &&
          cloned->arguments().size() == 2U &&
          cloned->arguments()[1].type() == *alternate &&
          cloned->ops().size() == 1U &&
          cloned->ops().front().callee() == *identity &&
          cloned->ops().front().value().type() == *alternate &&
          cloned->result_types() == std::vector<joggle::Type>{*alternate},
      "clone preserves arbitrary CFG while mapping types and generic Ops");

  joggle::Diagnostics inline_clone_diagnostics;
  const auto inline_clone = joggle::clone(
      compiler, *with_inline,
      [](const joggle::Value& value) -> std::optional<joggle::Type> {
        return value.type();
      },
      inline_clone_diagnostics);
  const auto cloned_arguments =
      inline_clone && inline_clone->ops().size() == 1U
          ? inline_clone->ops().front().arguments()
          : std::vector<joggle::Value>{};
  const auto cloned_body =
      cloned_arguments.size() == 2U
          ? cloned_arguments.back().inline_function()
          : std::optional<joggle::Function>{};
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
module staged_replace@1.0.0 {
  type word();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;
  fn converted(input: word) -> word;
  fn replace(input: function, before: function, after: function) -> function;
  fn inspect(input: function) -> int;

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
  const auto staged_replace_module = staged_replace.module("staged_replace");
  const auto staged_replace_decl =
      staged_replace_module ? staged_replace_module->function("replace")
                            : std::nullopt;
  const auto staged_optimize_decl =
      staged_replace_module ? staged_replace_module->function("optimize")
                            : std::nullopt;
  const auto staged_inspect_decl =
      staged_replace_module ? staged_replace_module->function("inspect")
                            : std::nullopt;
  const auto staged_converted_decl =
      staged_replace_module ? staged_replace_module->function("converted")
                            : std::nullopt;
  if (staged_replace_decl) {
    staged_replace.bind(
        *staged_replace_decl,
        [](joggle::Function input, const joggle::Function& before,
           const joggle::Function& after, joggle::Diagnostics& diagnostics)
            -> std::optional<joggle::Function> {
          const auto count = joggle::replace(input, before, after, diagnostics);
          return count ? std::optional<joggle::Function>{std::move(input)}
                       : std::nullopt;
        });
  }
  std::size_t inspected_replacement_calls = 0;
  std::optional<joggle::Module::FunctionDecl> inspected_replacement_callee;
  if (staged_inspect_decl) {
    staged_replace.bind(
        *staged_inspect_decl,
        [&](const joggle::Function& function) -> std::int64_t {
          inspected_replacement_calls = function.ops().size();
          if (!function.ops().empty()) {
            inspected_replacement_callee = function.ops().front().callee();
          }
          return static_cast<std::int64_t>(function.ops().size());
        });
  }
  const auto staged_optimized =
      staged_replace_linked && staged_optimize_decl && staged_replace_decl &&
              staged_inspect_decl
          ? staged_replace.materialize("staged_replace.optimize")
          : std::nullopt;
  if (!staged_optimized || !staged_replace.ok()) {
    staged_replace.diagnostics().print(std::cerr);
  }
  ok &= expect(staged_replace_linked && staged_replace_decl &&
                   staged_optimize_decl && staged_inspect_decl &&
                   staged_converted_decl &&
                   staged_optimized && staged_replace.ok() &&
                   staged_optimized->ops().size() == 1U &&
                   staged_optimized->ops().front().callee() ==
                       *staged_converted_decl &&
                   inspected_replacement_calls == 1U &&
                   inspected_replacement_callee == staged_converted_decl,
               "an ordinary source fn invokes typed-lambda replacement "
               "through explicit @ staging");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
