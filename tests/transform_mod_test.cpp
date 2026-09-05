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

constexpr std::string_view source = R"(
joggle 1;
mod transform_fixture@1.0.0 {
  import transform@2 as tr;
  import tensor@3 as t;

  type word();
  type memory();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;
  fn step(token: effect<memory>) -> effect<memory>;
  fn other_step(token: effect<memory>) -> effect<memory>;
  fn seed(position: t.coord<[4]>) -> f32;
  fn raise(value: f32) -> f32;

  fn chain(input: word) -> word {
    return other(keep(input));
  }

  fn wrapped(input: word) -> word {
    return chain(input);
  }

  fn expand(input: fn) -> fn {
    return @tr.inline(input);
  }

  fn expand(input: mod) -> mod {
    return @tr.inline(input);
  }

  fn reorder(input: fn) -> fn {
    return @tr.pass(
      input,
      (value: word) -> word => other(keep(value)),
      (value: word) -> word => keep(other(value))
    );
  }

  fn tensor_chain() -> t.tensor<f32, [4]> {
    built: t.tensor<f32, [4]> = t.build(
      [4],
      (position: t.coord<[4]>) => seed(position)
    );
    return t.map(
      built,
      (value: f32) => raise(value)
    );
  }

  fn fuse_tensor(input: fn) -> fn {
    return @tr.pass(
      input,
      (
        make: (t.coord<[4]>) -> f32,
        map: (f32) -> f32
      ) -> t.tensor<f32, [4]> => t.map(t.build([4], make), map),
      (
        make: (t.coord<[4]>) -> f32,
        map: (f32) -> f32
      ) -> t.tensor<f32, [4]> => t.build(
        [4],
        (position: t.coord<[4]>) => map(make(position))
      )
    );
  }

  fn sampled(position: t.coord<[4]>) -> f32 {
    built: t.tensor<f32, [4]> = t.build(
      [4],
      (item: t.coord<[4]>) => seed(item)
    );
    return t.at(built, position);
  }

  fn cancel_tensor(input: fn) -> fn {
    return @tr.pass(
      input,
      (
        make: (t.coord<[4]>) -> f32,
        position: t.coord<[4]>
      ) -> f32 => t.at(t.build([4], make), position),
      (
        make: (t.coord<[4]>) -> f32,
        position: t.coord<[4]>
      ) -> f32 => make(position)
    );
  }

  fn nested_sample() -> t.tensor<f32, [4]> {
    return t.build(
      [4],
      (position: t.coord<[4]>) => sampled(position)
    );
  }

  fn effect_chain(token: effect<memory>) -> effect<memory> {
    return step(token);
  }

  fn rewrite_effect(input: fn) -> fn {
    return @tr.pass(
      input,
      (token: effect<memory>) -> effect<memory> => step(token),
      (token: effect<memory>) -> effect<memory> => other_step(token)
    );
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TRANSFORM_MOD);
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.add(source, "transform-fixture.joggle");
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
      "a staged pass applies ordinary typed lambda equations and removes the "
      "replaced expression without operation-name dispatch");

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
          fused_calls.front().callee().referenced_fn()->name() == "build" &&
          fused_body && fused_body->ops().size() == 2U &&
          !fused_body->ops().front().callee().referenced_fn() &&
          !fused_body->ops().back().callee().referenced_fn() &&
          compiler.verify(*fused_tensor),
      "the same typed pass fuses map(build(S, f), g) into one build with a "
      "composed callable body");

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
               "at(build(S, f), p) cancels through an ordinary typed equation "
               "rather than a built-in tensor transform");

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
