#include <cstdlib>
#include <iostream>
#include <optional>
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

constexpr std::string_view algebra_source = R"(
joggle 1;
mod transform_algebra@1.0.0 {
  import tensor@4 as t;

  type word();
  type memory();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;
  fn step(token: effect<memory>) -> effect<memory>;
  fn other_step(token: effect<memory>) -> effect<memory>;
  fn seed(position: t.coord<[4]>) -> f32;
  fn raise(value: f32) -> f32;
  fn seed_word(position: t.coord<[2, 3]>) -> word;
  fn split_at<E, S: list<int>>(
    make: (t.coord<S>) -> E,
    position: t.coord<S>
  ) -> (E, E);
}
)";

constexpr std::string_view rules_source = R"(
joggle 1;
mod transform_rules@1.0.0 {
  import transform_algebra@1 as a;
  import tensor@4 as t;

  fn reorder(value: a.word) -> (a.word, a.word) {
    return a.other(a.keep(value)), a.keep(a.other(value));
  }

  fn fuse<E, S: list<int>>(
    make: (t.coord<S>) -> E,
    body: (E) -> E
  ) -> (t.tensor<E, S>, t.tensor<E, S>) {
    return
      t.map(t.map(S, make), body),
      t.map(S, (position: t.coord<S>) -> E => body(make(position)));
  }

  fn cancel<E, S: list<int>>(
    make: (t.coord<S>) -> E,
    position: t.coord<S>
  ) -> (E, E) {
    built: t.tensor<E, S> = t.map(S, make);
    return built[position], make(position);
  }

  fn second<E, S: list<int>>(
    make: (t.coord<S>) -> E,
    position: t.coord<S>
  ) -> (E, E) {
    first, second = a.split_at(make, position);
    return second, make(position);
  }
}
)";

constexpr std::string_view effect_source = R"(
joggle 1;
mod effect_rules@1.0.0 {
  import transform_algebra@1 as a;

  fn replace(token: effect<a.memory>)
      -> (effect<a.memory>, effect<a.memory>) {
    return a.step(token), a.other_step(token);
  }
}
)";

constexpr std::string_view fixture_source = R"(
joggle 1;
mod transform_fixture@1.0.0 {
  import transform@3 as tr;
  import transform_algebra@1 as a;
  import transform_rules@1 as rules;
  import effect_rules@1 as effects;
  import tensor@4 as t;

  fn chain(input: a.word) -> a.word {
    return a.other(a.keep(input));
  }

  fn wrapped(input: a.word) -> a.word {
    return chain(input);
  }

  fn expand(input: fn) -> fn {
    return @tr.inline(input);
  }

  fn expand(input: mod) -> mod {
    return @tr.inline(input);
  }

  fn reorder(input: fn) -> fn {
    return @tr.pass(input, rules);
  }

  fn local_reorder(value: a.word) -> (a.word, a.word) {
    return a.other(a.keep(value)), a.keep(a.other(value));
  }

  fn reorder_local(input: fn) -> fn {
    return @tr.pass(input, transform_fixture);
  }

  fn tensor_chain() -> t.tensor<f32, [4]> {
    built: t.tensor<f32, [4]> = t.map(
      [4],
      (position: t.coord<[4]>) => a.seed(position)
    );
    return t.map(
      built,
      (value: f32) => a.raise(value)
    );
  }

  fn fuse_tensor(input: fn) -> fn {
    return @tr.pass(input, rules);
  }

  fn tensor_chain_word() -> t.tensor<a.word, [2, 3]> {
    built: t.tensor<a.word, [2, 3]> = t.map(
      [2, 3],
      (position: t.coord<[2, 3]>) => a.seed_word(position)
    );
    return t.map(built, (value: a.word) => a.keep(value));
  }

  fn sampled(position: t.coord<[4]>) -> f32 {
    built: t.tensor<f32, [4]> = t.map(
      [4],
      (item: t.coord<[4]>) => a.seed(item)
    );
    return built[position];
  }

  fn sampled_word(position: t.coord<[2, 3]>) -> a.word {
    built: t.tensor<a.word, [2, 3]> = t.map(
      [2, 3],
      (item: t.coord<[2, 3]>) => a.seed_word(item)
    );
    return built[position];
  }

  fn cancel_tensor(input: fn) -> fn {
    return @tr.pass(input, rules);
  }

  fn use_second(position: t.coord<[2, 3]>) -> a.word {
    first, second = a.split_at(
      (item: t.coord<[2, 3]>) => a.seed_word(item),
      position
    );
    return second;
  }

  fn nested_sample() -> t.tensor<f32, [4]> {
    return t.map(
      [4],
      (position: t.coord<[4]>) => sampled(position)
    );
  }

  fn effect_chain(token: effect<a.memory>) -> effect<a.memory> {
    return a.step(token);
  }

  fn rewrite_effect(input: fn) -> fn {
    return @tr.pass(input, effects);
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TRANSFORM_MOD);
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.add(algebra_source, "transform-algebra.joggle");
  compiler.add(rules_source, "transform-rules.joggle");
  compiler.add(effect_source, "effect-rules.joggle");
  compiler.add(fixture_source, "transform-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("transform", JOGGLE_TRANSFORM_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto wrapped = compiler.materialize("transform_fixture.wrapped");
  const auto expanded =
      wrapped ? compiler.run<joggle::Fn>("transform_fixture.expand", *wrapped)
              : std::nullopt;
  if (!wrapped || !expanded) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto calls = expanded->ops();
  ok &= expect(calls.size() == 2U &&
                   calls.front().callee().referenced_fn()->name() == "keep" &&
                   calls.back().callee().referenced_fn()->name() == "other" &&
                   compiler.verify(*expanded),
               "one staged fn directly inlines a concrete source body");

  const auto reordered =
      compiler.run<joggle::Fn>("transform_fixture.reorder", *expanded);
  const auto reordered_calls =
      reordered ? reordered->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      reordered_calls.size() == 2U &&
          reordered_calls.front().callee().referenced_fn() &&
          reordered_calls.front().callee().referenced_fn()->name() == "other" &&
          reordered_calls.back().callee().referenced_fn() &&
          reordered_calls.back().callee().referenced_fn()->name() == "keep" &&
          reordered_calls.back().arguments() ==
              reordered_calls.front().results() &&
          compiler.verify(*reordered),
      "a staged pass applies ordinary packaged equations and removes the "
      "replaced expression without operation-name dispatch");

  const auto locally_reordered =
      compiler.run<joggle::Fn>("transform_fixture.reorder_local", *expanded);
  const auto local_calls =
      locally_reordered ? locally_reordered->ops() : std::vector<joggle::Op>{};
  ok &= expect(
      local_calls.size() == 2U &&
          local_calls.front().callee().referenced_fn() &&
          local_calls.front().callee().referenced_fn()->name() == "other" &&
          local_calls.back().callee().referenced_fn() &&
          local_calls.back().callee().referenced_fn()->name() == "keep" &&
          compiler.verify(*locally_reordered),
      "the current Mod name is a first-class equation package");

  const auto tensor_chain =
      compiler.materialize("transform_fixture.tensor_chain");
  const auto fused_tensor =
      tensor_chain ? compiler.run<joggle::Fn>("transform_fixture.fuse_tensor",
                                              *tensor_chain)
                   : std::nullopt;
  const auto fused_calls =
      fused_tensor ? fused_tensor->ops() : std::vector<joggle::Op>{};
  const auto fused_body =
      fused_calls.size() == 1U && !fused_calls.front().arguments().empty()
          ? fused_calls.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(
      fused_calls.size() == 1U &&
          fused_calls.front().callee().referenced_fn() &&
          fused_calls.front().callee().referenced_fn()->name() == "map" &&
          fused_body && fused_body->ops().size() == 2U &&
          !fused_body->ops().front().callee().referenced_fn() &&
          !fused_body->ops().back().callee().referenced_fn() &&
          compiler.verify(*fused_tensor),
      "the same typed pass fuses map(map(S, f), g) into one domain map with a "
      "composed callable body");

  const auto tensor_chain_word =
      compiler.materialize("transform_fixture.tensor_chain_word");
  const auto fused_tensor_word =
      tensor_chain_word
          ? compiler.run<joggle::Fn>("transform_fixture.fuse_tensor",
                                     *tensor_chain_word)
          : std::nullopt;
  const auto fused_word_calls =
      fused_tensor_word ? fused_tensor_word->ops() : std::vector<joggle::Op>{};
  const auto fused_word_body =
      fused_word_calls.size() == 1U &&
              !fused_word_calls.front().arguments().empty()
          ? fused_word_calls.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(
      fused_word_calls.size() == 1U &&
          fused_word_calls.front().callee().referenced_fn() &&
          fused_word_calls.front().callee().referenced_fn()->name() == "map" &&
          fused_word_body && fused_word_body->ops().size() == 2U &&
          compiler.verify(*fused_tensor_word),
      "one generic equation specializes to a different element Type and "
      "rank without a generated rule family");

  const auto sampled = compiler.materialize("transform_fixture.sampled");
  const auto cancelled =
      sampled ? compiler.run<joggle::Fn>("transform_fixture.cancel_tensor",
                                         *sampled)
              : std::nullopt;
  const auto cancelled_calls =
      cancelled ? cancelled->ops() : std::vector<joggle::Op>{};
  ok &= expect(cancelled_calls.size() == 1U &&
                   !cancelled_calls.front().callee().referenced_fn() &&
                   compiler.verify(*cancelled),
               "a result-underdetermined map(S, f)[p] equation infers its "
               "generic shape from the matched expression");

  const auto sampled_word =
      compiler.materialize("transform_fixture.sampled_word");
  const auto cancelled_word =
      sampled_word ? compiler.run<joggle::Fn>("transform_fixture.cancel_tensor",
                                              *sampled_word)
                   : std::nullopt;
  const auto cancelled_word_calls =
      cancelled_word ? cancelled_word->ops() : std::vector<joggle::Op>{};
  ok &= expect(cancelled_word_calls.size() == 1U &&
                   !cancelled_word_calls.front().callee().referenced_fn() &&
                   compiler.verify(*cancelled_word),
               "one structurally inferred equation specializes across "
               "unrelated element Types and ranks");

  const auto use_second = compiler.materialize("transform_fixture.use_second");
  const auto replaced_second =
      use_second
          ? compiler.run<joggle::Fn>("transform_fixture.reorder", *use_second)
          : std::nullopt;
  const auto second_calls =
      replaced_second ? replaced_second->ops() : std::vector<joggle::Op>{};
  ok &= expect(second_calls.size() == 1U &&
                   !second_calls.front().callee().referenced_fn() &&
                   compiler.verify(*replaced_second),
               "generic inference follows a local name to one selected "
               "result of an ordinary multi-result call");

  auto nested_sample = compiler.materialize("transform_fixture.nested_sample");
  joggle::Diag nested_diagnostics;
  const auto nested_inline =
      nested_sample
          ? joggle::inline_calls(compiler, *nested_sample, nested_diagnostics)
          : std::nullopt;
  const auto nested_cancelled =
      nested_sample ? compiler.run<joggle::Fn>(
                          "transform_fixture.cancel_tensor", *nested_sample)
                    : std::nullopt;
  const auto nested_builds =
      nested_cancelled ? nested_cancelled->ops() : std::vector<joggle::Op>{};
  const auto nested_body =
      nested_builds.size() == 1U && !nested_builds.front().arguments().empty()
          ? nested_builds.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(nested_inline == std::optional<std::size_t>{1U} &&
                   nested_diagnostics.ok() && nested_body &&
                   nested_body->ops().size() == 1U &&
                   !nested_body->ops().front().callee().referenced_fn() &&
                   compiler.verify(*nested_cancelled),
               "the same pass descends into an existing callable body and "
               "publishes its replacement through ordinary capture edges");

  joggle::Diag diagnostics;
  auto subject = joggle::parse_mod("joggle 1; mod subject@1.0.0 {}",
                                   diagnostics, "subject.joggle");
  if (!subject || !subject->insert("main", joggle::Fn{*wrapped}, diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto expanded_mod =
      compiler.run<joggle::Mod>("transform_fixture.expand", *subject);
  const auto expanded_main = expanded_mod
                                 ? expanded_mod->fn("main")
                                 : std::optional<joggle::Mod::FnDecl>{};
  const joggle::Fn* expanded_body =
      expanded_main ? expanded_main->body() : nullptr;
  ok &= expect(expanded_body && expanded_body->ops().size() == 2U &&
                   compiler.verify(*expanded_mod),
               "the Mod overload publishes all changed Fn bodies atomically");

  auto resolve_subject =
      joggle::parse_mod("joggle 1; mod resolve_subject@1.0.0 {}", diagnostics,
                        "resolve-subject.joggle");
  if (!resolve_subject ||
      !resolve_subject->insert("main", joggle::Fn{*wrapped}, diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto resolved =
      compiler.run<joggle::Mod>("transform.resolve", *resolve_subject);
  const auto resolved_main =
      resolved ? resolved->fn("main") : std::optional<joggle::Mod::FnDecl>{};
  const auto resolved_chain = resolved ? resolved->fn("inst_0_chain")
                                       : std::optional<joggle::Mod::FnDecl>{};
  const joggle::Fn* resolved_main_body =
      resolved_main ? resolved_main->body() : nullptr;
  const joggle::Fn* resolved_chain_body =
      resolved_chain ? resolved_chain->body() : nullptr;
  ok &= expect(resolved && resolved_main_body && resolved_chain_body &&
                   resolved_main_body->ops().size() == 1U &&
                   resolved_main_body->ops().front().callee().referenced_fn() ==
                       resolved_chain &&
                   resolved_chain_body->ops().size() == 2U &&
                   compiler.verify(*resolved),
               "source resolution retains a closed, explicit call graph");

  const auto effect_chain =
      compiler.materialize("transform_fixture.effect_chain");
  const auto rejected_effect =
      effect_chain ? compiler.run<joggle::Fn>(
                         "transform_fixture.rewrite_effect", *effect_chain)
                   : std::nullopt;
  const bool reports_effect = std::any_of(
      compiler.diag().issues().begin(), compiler.diag().issues().end(),
      [](const joggle::Issue& issue) {
        return issue.message.find("pass equations cannot contain effects") !=
               std::string::npos;
      });
  ok &= expect(effect_chain && !rejected_effect && reports_effect,
               "typed passes reject effectful equations before editing IR");

  if (!ok || !diagnostics.ok()) {
    diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok && diagnostics.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
