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

  type word();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;

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

  if (!ok || !diagnostics.ok()) {
    diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok && diagnostics.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
