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

module transform_fixture@1.0.0 {
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

  fn optimize(input: function) -> function {
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
  compiler.load(JOGGLE_TRANSFORM_MODULE);
  compiler.add(source, "transform-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("transform", JOGGLE_TRANSFORM_NATIVE)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto model = compiler.materialize("transform_fixture.model");
  const auto wrapped = compiler.materialize("transform_fixture.wrapped");
  const auto optimized = model ? compiler.run<joggle::Function>(
                                     "transform_fixture.optimize", *model)
                               : std::nullopt;
  if (!model || !wrapped || !optimized) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto calls = optimized->ops();
  joggle::Diagnostics equivalence;
  bool ok = true;
  ok &= expect(calls.size() == 1U &&
                   calls.front().callee().symbol().module_name() ==
                       "transform_fixture" &&
                   calls.front().callee().symbol().local_name() == "chain",
               "a source pipeline invokes typed-lambda replacement directly");
  ok &= expect(joggle::equivalent(compiler, *model, *optimized,
                                  equivalence) &&
                   equivalence.ok(),
               "the public transform Module preserves reference meaning");
  ok &= expect(compiler.verify(*optimized),
               "the transformed Function remains valid executable IR");

  joggle::Diagnostics module_diagnostics;
  auto subject = joggle::parse_module(
      "joggle 1; module transform_subject@1.0.0 {}",
      module_diagnostics, "transform-subject.joggle");
  if (!subject ||
      !subject->insert("main", joggle::Function{*model},
                       module_diagnostics)) {
    module_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto optimized_module = compiler.run<joggle::Module>(
      "transform.replace", *subject, *model, *wrapped);
  const auto optimized_main =
      optimized_module ? optimized_module->function("main") : std::nullopt;
  const joggle::Function* optimized_body =
      optimized_main ? optimized_main->body() : nullptr;
  const auto module_calls = optimized_body ? optimized_body->ops()
                                           : std::vector<joggle::Op>{};
  ok &= expect(module_diagnostics.ok() && optimized_body != nullptr &&
                   module_calls.size() == 1U &&
                   module_calls.front().callee().symbol().local_name() ==
                       "chain",
               "the public Module overload publishes one transformed "
               "snapshot");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
