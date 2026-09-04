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

  fn optimize(input: fn) -> fn {
    return @tr.replace(
      input,
      (value: word) => other(keep(value)),
      (value: word) => chain(value)
    );
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
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
  if (!model || !wrapped || !optimized) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto calls = optimized->ops();
  joggle::Diag equivalence;
  bool ok = true;
  ok &= expect(calls.size() == 1U &&
                   calls.front().callee().symbol().mod_name() ==
                       "transform_fixture" &&
                   calls.front().callee().symbol().local_name() == "chain",
               "a source pipeline invokes typed-lambda replacement directly");
  ok &= expect(joggle::equivalent(compiler, *model, *optimized, equivalence) &&
                   equivalence.ok(),
               "the public transform Mod preserves reference meaning");
  ok &= expect(compiler.verify(*optimized),
               "the transformed Fn remains valid executable IR");

  joggle::Diag mod_diagnostics;
  auto subject = joggle::parse_mod("joggle 1; mod transform_subject@1.0.0 {}",
                                   mod_diagnostics, "transform-subject.joggle");
  if (!subject ||
      !subject->insert("main", joggle::Fn{*model}, mod_diagnostics)) {
    mod_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto optimized_mod = compiler.run<joggle::Mod>(
      "transform.replace", *subject, *model, *wrapped);
  const auto optimized_main =
      optimized_mod ? optimized_mod->fn("main") : std::nullopt;
  const joggle::Fn* optimized_body =
      optimized_main ? optimized_main->body() : nullptr;
  const auto mod_calls =
      optimized_body ? optimized_body->ops() : std::vector<joggle::Op>{};
  ok &= expect(mod_diagnostics.ok() && optimized_body != nullptr &&
                   mod_calls.size() == 1U &&
                   mod_calls.front().callee().symbol().local_name() == "chain",
               "the public Mod overload publishes one transformed "
               "snapshot");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
