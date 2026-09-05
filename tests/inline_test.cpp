#include <algorithm>
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
  pub type word();
  pub type memory();

  pub fn leaf(input: word) -> word;
  pub fn join(lhs: word, rhs: word) -> word;
  pub fn apply(input: word, body: (word) -> word) -> word;
  pub fn step(token: effect<memory>) -> effect<memory>;

  fn invoke(input: word, body: (word) -> word) -> word {
    return body(input);
  }

  pub fn twice(input: word) -> word {
    first = leaf(input);
    return leaf(first);
  }

  pub fn caller(input: word) -> word {
    return twice(input);
  }

  pub fn opaque(input: word) -> word {
    return leaf(input);
  }

  pub fn choose(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { leaf(lhs) } else { leaf(rhs) };
  }

  pub fn choose_caller(condition: i1, lhs: word, rhs: word) -> word {
    return choose(condition, lhs, rhs);
  }

  pub fn nested(input: word) -> word {
    return leaf(input);
  }

  pub fn closure(input: word) -> word {
    return apply(input, (value: word) => join(nested(value), input));
  }

  pub fn closure_caller(input: word) -> word {
    return closure(input);
  }

  pub fn branch_closure(condition: i1, input: word) -> word {
    return invoke(
      input,
      (value: word) => if condition { leaf(value) } else { leaf(input) }
    );
  }

  pub fn advance(token: effect<memory>) -> effect<memory> {
    token = step(token);
    return step(token);
  }

  pub fn effect_caller(token: effect<memory>) -> effect<memory> {
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
  auto branch_closure =
      compiler.materialize("inline_fixture.branch_closure");
  auto effect = compiler.materialize("inline_fixture.effect_caller");
  if (!caller || !opaque || !choose || !closure || !branch_closure ||
      !effect) {
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
  const auto choose_ops = choose->ops();
  ok &= expect(
      choose_count == std::optional<std::size_t>{1U} &&
          choose->revision() != choose_revision && choose->blks().size() > 1U &&
          choose_ops.size() == 2U &&
          std::all_of(choose_ops.begin(), choose_ops.end(),
                      [](const joggle::Op& op) {
                        const auto fn = op.callee().referenced_fn();
                        return fn && fn->name() == "leaf";
                      }) &&
          compiler.verify(*choose),
      "CFG inlining splices branches and joins through typed continuation "
      "arguments");

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

  const auto branch_closure_outer =
      joggle::inline_calls(compiler, *branch_closure, diagnostics);
  const auto branch_closure_inner =
      joggle::inline_calls(compiler, *branch_closure, diagnostics);
  const auto branch_closure_ops = branch_closure->ops();
  ok &= expect(
      branch_closure_outer == std::optional<std::size_t>{1U} &&
          branch_closure_inner == std::optional<std::size_t>{1U} &&
          branch_closure->blks().size() > 1U &&
          branch_closure_ops.size() == 2U &&
          std::all_of(branch_closure_ops.begin(), branch_closure_ops.end(),
                      [](const joggle::Op& op) {
                        const auto fn = op.callee().referenced_fn();
                        return fn && fn->name() == "leaf";
                      }) &&
          compiler.verify(*branch_closure),
      "CFG inlining handles an anonymous branch body and remaps its captures");

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
