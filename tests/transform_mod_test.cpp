#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
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

std::string callee(const joggle::Op& op) {
  const auto fn = op.callee().referenced_fn();
  return fn ? std::string(fn->name()) : std::string{};
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TRANSFORM_MOD);
  compiler.add(R"(
joggle 1;
mod algebra@1.0.0 {
  type word();
  fn keep(input: word) -> word;
  fn other(input: word) -> word;
}
)",
               "algebra.joggle");
  compiler.add(R"(
joggle 1;
mod laws@1.0.0 {
  import algebra@1 as a;
  fn commute(value: a.word) -> (a.word, a.word) {
    return a.other(a.keep(value)), a.keep(a.other(value));
  }
}
)",
               "laws.joggle");
  compiler.add(R"(
joggle 1;
mod fixture@1.0.0 {
  import algebra@1 as a;
  import laws@1 as laws;
  import transform@3 as tr;

  fn chain(input: a.word) -> a.word {
    return a.other(a.keep(input));
  }
  fn wrapped(input: a.word) -> a.word {
    return chain(input);
  }
  fn rewrite(input: fn) -> fn {
    return @tr.pass(input, laws);
  }
  fn expand(input: fn) -> fn {
    return @tr.inline(input);
  }
  fn close(input: mod) -> mod {
    return @tr.resolve(input);
  }
}
)",
               "fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("transform", JOGGLE_TRANSFORM_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto chain = compiler.materialize("fixture.chain");
  const auto rewritten = chain
                             ? compiler.run<joggle::Fn>("fixture.rewrite", *chain)
                             : std::optional<joggle::Fn>{};
  const auto rewritten_ops = rewritten ? rewritten->ops()
                                       : std::vector<joggle::Op>{};
  ok &= expect(rewritten_ops.size() == 2U &&
                   callee(rewritten_ops[0]) == "other" &&
                   callee(rewritten_ops[1]) == "keep" &&
                   compiler.verify(*rewritten),
               "a pass is an ordinary staged fn over an equation mod");

  const auto wrapped = compiler.materialize("fixture.wrapped");
  const auto expanded = wrapped
                            ? compiler.run<joggle::Fn>("fixture.expand", *wrapped)
                            : std::optional<joggle::Fn>{};
  const auto expanded_ops = expanded ? expanded->ops() : std::vector<joggle::Op>{};
  ok &= expect(expanded_ops.size() == 2U && callee(expanded_ops[0]) == "keep" &&
                   callee(expanded_ops[1]) == "other" &&
                   compiler.verify(*expanded),
               "generic inlining exposes an ordinary Fn body");

  joggle::Diag diagnostics;
  auto subject = joggle::parse_mod("joggle 1; mod subject@1.0.0 {}",
                                   diagnostics, "subject.joggle");
  if (!subject || !wrapped ||
      !subject->insert("main", joggle::Fn{*wrapped}, diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto closed = compiler.run<joggle::Mod>("fixture.close", *subject);
  ok &= expect(closed && closed->fns().size() == 2U && compiler.verify(*closed),
               "resolution publishes a closed explicit call graph");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
