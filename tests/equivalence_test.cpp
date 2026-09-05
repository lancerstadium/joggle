#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.add(R"(
joggle 1;
mod semantics@1.0.0 {
  type word();
  type represented(logical: type);

  fn primitive(input: word, axis: int) -> word;
  fn other(input: word, axis: int) -> word;
  fn interleaved(axis: int, input: word, scale: int) -> word;
  fn combine(lhs: word, rhs: word) -> word;
  fn step<T>(input: T) -> T;
  fn replace(input: fn, before: fn, after: fn) -> fn;

  fn wrapped(input: word, axis: int) -> word {
    return primitive(input, axis);
  }

  fn twice(input: word) -> word {
    return wrapped(wrapped(input, 1), 2);
  }

  fn recursive(input: word) -> word {
    return recursive(input);
  }

  fn interleaved_wrapper(axis: int, input: word, scale: int) -> word {
    return interleaved(axis, input, scale);
  }

  fn direct(input: word) -> word {
    return primitive(primitive(input, 1), 2);
  }

  fn through_wrapper(input: word) -> word {
    return twice(input);
  }

  fn wrong_property(input: word) -> word {
    return primitive(primitive(input, 1), 3);
  }

  fn wrong_callee(input: word) -> word {
    return other(primitive(input, 1), 2);
  }

  fn recursive_use(input: word) -> word {
    return recursive(input);
  }

  fn interleaved_direct(input: word) -> word {
    return interleaved(3, input, 7);
  }

  fn interleaved_indirect(input: word) -> word {
    return interleaved_wrapper(3, input, 7);
  }

  fn shared_direct(input: word) -> word {
    value: word = primitive(input, 1);
    return combine(value, value);
  }

  fn shared_indirect(input: word) -> word {
    value: word = wrapped(input, 1);
    return combine(value, value);
  }

  fn logical(input: word) -> word {
    return step(input);
  }

  fn physical(input: represented<word>) -> represented<word> {
    return step(input);
  }

  fn subject(input: word) -> word {
    return primitive(primitive(input, 1), 2);
  }

  fn apply(input: fn) -> fn {
    return @replace(
      input,
      (x: word) => primitive(primitive(x, 1), 2),
      (x: word) => twice(x)
    );
  }

  fn pipeline(input: fn) -> fn {
    return @apply(input);
  }
}
)",
               "semantics.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto semantics = compiler.mod("semantics");
  if (!semantics) {
    return EXIT_FAILURE;
  }
  const auto word_decl = semantics->type("word");
  const auto represented_decl = semantics->type("represented");
  const auto word = word_decl ? compiler.make(*word_decl) : std::nullopt;
  const auto represented = represented_decl && word
                               ? compiler.make(*represented_decl, *word)
                               : std::nullopt;
  if (!word_decl || !represented_decl || !word || !represented) {
    return EXIT_FAILURE;
  }
  std::optional<joggle::Fn> staged_replacement;
  compiler.bind(*semantics, "replace",
                [&](joggle::Compiler& active, joggle::Fn input,
                    const joggle::Fn& before, const joggle::Fn& after,
                    joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
                  const auto changed = joggle::replace(active, input, before,
                                                       after, diagnostics);
                  if (changed) {
                    staged_replacement = input;
                  }
                  return changed ? std::optional<joggle::Fn>{std::move(input)}
                                 : std::nullopt;
                });

  const auto direct = compiler.materialize("semantics.direct");
  const auto wrapped = compiler.materialize("semantics.through_wrapper");
  const auto wrong_property = compiler.materialize("semantics.wrong_property");
  const auto wrong_callee = compiler.materialize("semantics.wrong_callee");
  const auto recursive = compiler.materialize("semantics.recursive_use");
  const auto interleaved_direct =
      compiler.materialize("semantics.interleaved_direct");
  const auto interleaved_indirect =
      compiler.materialize("semantics.interleaved_indirect");
  const auto shared_direct = compiler.materialize("semantics.shared_direct");
  const auto shared_indirect =
      compiler.materialize("semantics.shared_indirect");
  const auto logical = compiler.materialize("semantics.logical");
  const auto physical = compiler.materialize("semantics.physical");
  auto subject = compiler.materialize("semantics.subject");
  const auto staged_subject = compiler.materialize("semantics.subject");
  if (!direct || !wrapped || !wrong_property || !wrong_callee || !recursive ||
      !interleaved_direct || !interleaved_indirect || !subject ||
      !shared_direct || !shared_indirect || !logical || !physical ||
      !staged_subject) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto staged_apply =
      compiler.run<joggle::Fn>("semantics.pipeline", *staged_subject);
  if (!staged_apply) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Diag equivalent_diagnostics;
  ok &= expect(
      joggle::equivalent(compiler, *direct, *wrapped, equivalent_diagnostics) &&
          equivalent_diagnostics.ok(),
      "nested source bodies normalize to their reference meaning");

  joggle::Diag property_diagnostics;
  ok &= expect(!joggle::equivalent(compiler, *direct, *wrong_property,
                                   property_diagnostics) &&
                   !property_diagnostics.ok(),
               "Known callee bindings remain part of opaque call identity");

  joggle::Diag callee_diagnostics;
  ok &= expect(!joggle::equivalent(compiler, *direct, *wrong_callee,
                                   callee_diagnostics) &&
                   !callee_diagnostics.ok(),
               "opaque overload identity remains semantic");

  joggle::Diag recursion_diagnostics;
  ok &= expect(!joggle::equivalent(compiler, *direct, *recursive,
                                   recursion_diagnostics) &&
                   !recursion_diagnostics.ok(),
               "recursive reference semantics fail closed");

  joggle::Diag interleaved_diagnostics;
  ok &= expect(joggle::equivalent(compiler, *interleaved_direct,
                                  *interleaved_indirect,
                                  interleaved_diagnostics) &&
                   interleaved_diagnostics.ok(),
               "source expansion maps runtime arguments independently of "
               "callee bindings");

  joggle::Diag shared_dag_diagnostics;
  ok &= expect(joggle::equivalent(compiler, *shared_direct, *shared_indirect,
                                  shared_dag_diagnostics, 1U) &&
                   shared_dag_diagnostics.ok(),
               "shared DAG values are normalized once rather than expanded "
               "as a tree");

  joggle::Diag exact_representation_diagnostics;
  ok &= expect(!joggle::equivalent(compiler, *logical, *physical,
                                   exact_representation_diagnostics),
               "exact equivalence rejects different physical signatures");

  const auto revision = subject->revision();
  joggle::Diag rejected_diagnostics;
  const auto rejected = joggle::replace(compiler, *subject, *direct,
                                        *wrong_property, rejected_diagnostics);
  ok &= expect(!rejected && !rejected_diagnostics.ok() &&
                   subject->revision() == revision,
               "an unproved replacement publishes no edit");

  joggle::Diag replacement_diagnostics;
  const auto replaced = joggle::replace(compiler, *subject, *direct, *wrapped,
                                        replacement_diagnostics);
  ok &= expect(replaced && *replaced == 1U && replacement_diagnostics.ok() &&
                   subject->ops().size() == 1U &&
                   subject->ops().front().callee().referenced_fn()->name() ==
                       "twice",
               "a proved replacement commits atomically");

  ok &=
      expect(staged_replacement && staged_apply->ops().size() == 1U &&
                 staged_apply->ops().front().callee().referenced_fn()->name() ==
                     "twice" &&
                 staged_replacement->revision() == staged_apply->revision(),
             "compiler Fn values and typed lambdas compose through "
             "ordinary explicitly staged source fns");

  joggle::Diag limit_diagnostics;
  ok &= expect(
      !joggle::equivalent(compiler, *direct, *wrapped, limit_diagnostics, 1U) &&
          !limit_diagnostics.ok(),
      "source expansion obeys an explicit bound");

  joggle::Diag exact_limit_diagnostics;
  ok &= expect(joggle::equivalent(compiler, *direct, *wrapped,
                                  exact_limit_diagnostics, 4U) &&
                   exact_limit_diagnostics.ok(),
               "each source call consumes exactly one expansion step");

  ok &= expect(compiler.diag().ok(),
               "local equivalence failures do not poison compiler state");
  if (!ok) {
    equivalent_diagnostics.print(std::cerr);
    property_diagnostics.print(std::cerr);
    callee_diagnostics.print(std::cerr);
    recursion_diagnostics.print(std::cerr);
    interleaved_diagnostics.print(std::cerr);
    shared_dag_diagnostics.print(std::cerr);
    exact_representation_diagnostics.print(std::cerr);
    rejected_diagnostics.print(std::cerr);
    replacement_diagnostics.print(std::cerr);
    limit_diagnostics.print(std::cerr);
    exact_limit_diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
