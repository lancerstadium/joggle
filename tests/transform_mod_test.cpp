#include <cstdlib>
#include <iostream>
#include <string_view>

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
  import transform@1 as tr;
  import tensor@1 as t;

  type word();

  fn keep(input: word) -> word;
  fn other(input: word) -> word;

  fn chain(input: word) -> word {
    return other(keep(input));
  }

  fn model(input: word) -> word {
    return other(keep(input));
  }

  fn wrapped(input: word) -> word {
    return chain(input);
  }

  fn factor(input: fn) -> fn {
    return @tr.replace(
      input,
      (value: word) => other(keep(value)),
      (value: word) => chain(value)
    );
  }

  fn wrap(input: fn) -> fn {
    return @tr.replace(
      input,
      (value: word) => chain(value),
      (value: word) => wrapped(value)
    );
  }

  fn optimize(input: fn) -> fn {
    factored = @factor(input);
    return @wrap(factored);
  }

  fn factor(input: mod) -> mod {
    return @tr.replace(
      input,
      (value: word) => other(keep(value)),
      (value: word) => chain(value)
    );
  }

  fn wrap(input: mod) -> mod {
    return @tr.replace(
      input,
      (value: word) => chain(value),
      (value: word) => wrapped(value)
    );
  }

  fn optimize(input: mod) -> mod {
    factored = @factor(input);
    return @wrap(factored);
  }

  fn conv_relu(
    input: t.tensor<f32, [1, 3, 8, 8]>,
    weight: t.tensor<f32, [4, 3, 3, 3]>,
    bias: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [1, 4, 6, 6]> {
    convolved: t.tensor<f32, [1, 4, 6, 6]> = t.conv(
      input, weight, bias, [1, 1], [0, 0, 0, 0], [1, 1], 1
    );
    return t.relu(convolved);
  }

  fn conv_model(
    input: t.tensor<f32, [1, 3, 8, 8]>,
    weight: t.tensor<f32, [4, 3, 3, 3]>,
    bias: t.tensor<f32, [4]>
  ) -> t.tensor<f32, [1, 4, 6, 6]> {
    convolved: t.tensor<f32, [1, 4, 6, 6]> = t.conv(
      input, weight, bias, [1, 1], [0, 0, 0, 0], [1, 1], 1
    );
    return t.relu(convolved);
  }

  fn factor_conv(input: fn) -> fn {
    return @tr.replace(
      input,
      (
        value: t.tensor<f32, [1, 3, 8, 8]>,
        weight: t.tensor<f32, [4, 3, 3, 3]>,
        bias: t.tensor<f32, [4]>
      ) -> t.tensor<f32, [1, 4, 6, 6]> => t.relu(t.conv(
        value, weight, bias, [1, 1], [0, 0, 0, 0], [1, 1], 1
      )),
      (
        value: t.tensor<f32, [1, 3, 8, 8]>,
        weight: t.tensor<f32, [4, 3, 3, 3]>,
        bias: t.tensor<f32, [4]>
      ) -> t.tensor<f32, [1, 4, 6, 6]> => conv_relu(
        value, weight, bias
      )
    );
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.load(JOGGLE_TRANSFORM_MOD);
  compiler.add(source, "transform-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("transform", JOGGLE_TRANSFORM_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto model = compiler.materialize("transform_fixture.model");
  const auto wrapped = compiler.materialize("transform_fixture.wrapped");
  const auto optimized =
      model ? compiler.run<joggle::Fn>("transform_fixture.optimize", *model)
            : std::nullopt;
  const auto conv_model =
      compiler.materialize("transform_fixture.conv_model");
  const auto factored_conv =
      conv_model ? compiler.run<joggle::Fn>("transform_fixture.factor_conv",
                                             *conv_model)
                 : std::nullopt;
  if (!model || !wrapped || !optimized || !conv_model || !factored_conv) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto calls = optimized->ops();
  joggle::Diag equivalence;
  bool ok = true;
  ok &= expect(calls.size() == 1U &&
                   calls.front().callee().symbol().mod_name() ==
                       "transform_fixture" &&
                   calls.front().callee().symbol().local_name() == "wrapped",
               "several source-defined passes compose without one native "
               "binding per pass");
  ok &= expect(joggle::equivalent(compiler, *model, *optimized, equivalence) &&
                   equivalence.ok(),
               "the public transform Mod preserves reference meaning");
  ok &= expect(compiler.verify(*optimized),
               "the transformed Fn remains valid executable IR");
  ok &= expect(factored_conv->ops().size() == 1U &&
                   factored_conv->ops().front().callee().symbol().local_name() ==
                       "conv_relu" &&
                   compiler.verify(*factored_conv),
               "a lambda result annotation supplies enough context to match "
               "and factor a generic tensor expression");

  joggle::Diag mod_diagnostics;
  auto subject = joggle::parse_mod("joggle 1; mod transform_subject@1.0.0 {}",
                                   mod_diagnostics, "transform-subject.joggle");
  if (!subject ||
      !subject->insert("main", joggle::Fn{*model}, mod_diagnostics)) {
    mod_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto optimized_mod =
      compiler.run<joggle::Mod>("transform_fixture.optimize", *subject);
  const auto optimized_main =
      optimized_mod ? optimized_mod->fn("main") : std::nullopt;
  const joggle::Fn* optimized_body =
      optimized_main ? optimized_main->body() : nullptr;
  const auto mod_calls =
      optimized_body ? optimized_body->ops() : std::vector<joggle::Op>{};
  ok &= expect(mod_diagnostics.ok() && optimized_body != nullptr &&
                   mod_calls.size() == 1U &&
                   mod_calls.front().callee().symbol().local_name() ==
                       "wrapped",
               "a source-defined whole-Mod pipeline publishes one "
               "transformed snapshot without additional bindings");

  joggle::Diag resolve_diagnostics;
  auto resolve_subject = joggle::parse_mod(
      "joggle 1; mod resolve_subject@1.0.0 {}", resolve_diagnostics,
      "resolve-subject.joggle");
  if (!resolve_subject ||
      !resolve_subject->insert("main", joggle::Fn{*wrapped},
                               resolve_diagnostics)) {
    resolve_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto resolved = compiler.run<joggle::Mod>("transform.resolve",
                                                   *resolve_subject);
  const auto resolved_main = resolved ? resolved->fn("main") : std::nullopt;
  const auto resolved_chain =
      resolved ? resolved->fn("inst_0_chain") : std::nullopt;
  const joggle::Fn* resolved_main_body =
      resolved_main ? resolved_main->body() : nullptr;
  const joggle::Fn* resolved_chain_body =
      resolved_chain ? resolved_chain->body() : nullptr;
  ok &= expect(resolved && resolved_main_body && resolved_chain_body &&
                   resolved_main_body->ops().size() == 1U &&
                   resolved_main_body->ops().front().callee() ==
                       *resolved_chain &&
                   resolved_chain_body->ops().size() == 2U &&
                   compiler.verify(*resolved),
               "one staged Mod fn resolves the complete source call graph");
  if (!ok) {
    equivalence.print(std::cerr);
    resolve_diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
