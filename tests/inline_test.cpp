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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.add(R"(
joggle 1;
mod inline_fixture@1.0.0 {
  type word();
  type memory();

  fn leaf(input: word) -> word;
  fn join(lhs: word, rhs: word) -> word;
  fn apply(input: word, body: (word) -> word) -> word;
  fn step(token: effect<memory>) -> effect<memory>;

  fn twice(input: word) -> word {
    first = leaf(input);
    return leaf(first);
  }

  fn caller(input: word) -> word {
    return twice(input);
  }

  fn opaque(input: word) -> word {
    return leaf(input);
  }

  fn choose(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { leaf(lhs) } else { leaf(rhs) };
  }

  fn choose_caller(condition: i1, lhs: word, rhs: word) -> word {
    return choose(condition, lhs, rhs);
  }

  fn nested(input: word) -> word {
    return leaf(input);
  }

  fn closure(input: word) -> word {
    return apply(input, (value: word) => join(nested(value), input));
  }

  fn closure_caller(input: word) -> word {
    return closure(input);
  }

  fn advance(token: effect<memory>) -> effect<memory> {
    token = step(token);
    return step(token);
  }

  fn effect_caller(token: effect<memory>) -> effect<memory> {
    return advance(token);
  }
}
)",
               "inline-fixture.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  auto caller = compiler.materialize("inline_fixture.caller");
  auto opaque = compiler.materialize("inline_fixture.opaque");
  auto choose = compiler.materialize("inline_fixture.choose_caller");
  auto closure = compiler.materialize("inline_fixture.closure_caller");
  auto effect = compiler.materialize("inline_fixture.effect_caller");
  if (!caller || !opaque || !choose || !closure || !effect) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Diag diagnostics;
  const auto caller_revision = caller->revision();
  const auto caller_count =
      joggle::inline_calls(compiler, *caller, diagnostics);
  ok &= expect(
      caller_count == std::optional<std::size_t>{1U} &&
          caller->revision() != caller_revision && caller->ops().size() == 2U &&
          caller->ops().front().callee().referenced_fn()->name() == "leaf" &&
          caller->ops().back().arguments() == caller->ops().front().results() &&
          compiler.verify(*caller),
      "single-block source calls inline into concrete caller Ops");

  const auto opaque_revision = opaque->revision();
  const auto opaque_count =
      joggle::inline_calls(compiler, *opaque, diagnostics);
  ok &= expect(opaque_count == std::optional<std::size_t>{0U} &&
                   opaque->revision() == opaque_revision &&
                   opaque->ops().size() == 1U,
               "opaque leaves remain unchanged without publishing an edit");

  const auto choose_revision = choose->revision();
  const auto choose_count =
      joggle::inline_calls(compiler, *choose, diagnostics);
  ok &= expect(choose_count == std::optional<std::size_t>{0U} &&
                   choose->revision() == choose_revision &&
                   choose->ops().size() == 1U,
               "multi-block callees remain structured until CFG inlining is "
               "implemented");

  const auto closure_count =
      joggle::inline_calls(compiler, *closure, diagnostics);
  const auto closure_ops = closure->ops();
  const auto callback =
      closure_ops.size() == 1U && closure_ops.front().arguments().size() == 2U
          ? std::optional<joggle::Val>{closure_ops.front().arguments().back()}
          : std::nullopt;
  ok &= expect(closure_count == std::optional<std::size_t>{1U} && callback &&
                   callback->inline_fn() &&
                   callback->captures() == closure->arguments() &&
                   compiler.verify(*closure),
               "inlining clones nested callable bodies and remaps their "
               "explicit captures");

  const auto nested_count =
      joggle::inline_calls(compiler, *closure, diagnostics);
  const auto nested_ops = closure->ops();
  const auto nested_callback =
      nested_ops.size() == 1U && nested_ops.front().arguments().size() == 2U
          ? nested_ops.front().arguments().back().inline_fn()
          : std::optional<joggle::Fn>{};
  ok &= expect(
      nested_count == std::optional<std::size_t>{1U} && nested_callback &&
          nested_callback->ops().size() == 2U &&
          nested_callback->ops().front().callee().referenced_fn() &&
          nested_callback->ops().front().callee().referenced_fn()->name() ==
              "leaf" &&
          compiler.verify(*closure),
      "generic inlining recurses through existing typed lambda "
      "bodies without a separate nested IR");

  const auto effect_count =
      joggle::inline_calls(compiler, *effect, diagnostics);
  ok &= expect(effect_count == std::optional<std::size_t>{1U} &&
                   effect->ops().size() == 2U && compiler.verify(*effect),
               "inlining preserves explicit affine effect-token flow");

  if (!ok || !diagnostics.ok()) {
    diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok && diagnostics.ok() ? EXIT_SUCCESS : EXIT_FAILURE;
}
