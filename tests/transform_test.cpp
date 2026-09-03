#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

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

module mapping@1.0.0 {
  type word();

  fn keep(input: word) -> word;
  fn converted(input: word) -> word;
  fn other(input: word) -> word;
  fn binary(lhs: word, rhs: word) -> word;

  fn first(input: word) -> word {
    return keep(input);
  }

  fn second(input: word) -> word {
    return other(input);
  }
}
)",
               "mapping.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto module = compiler.module("mapping");
  const auto keep = module ? module->function("keep") : std::nullopt;
  const auto converted = module ? module->function("converted") : std::nullopt;
  const auto other = module ? module->function("other") : std::nullopt;
  const auto binary = module ? module->function("binary") : std::nullopt;
  auto first = compiler.function("mapping.first");
  auto second = compiler.function("mapping.second");
  if (!keep || !converted || !other || !binary || !first || !second) {
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto first_revision = first->revision();
  joggle::Diagnostics no_op_diagnostics;
  const auto no_op = joggle::ir::map_calls(
      *first,
      [](const joggle::ir::Instruction&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      no_op_diagnostics);
  ok &= expect(no_op && *no_op == 0U && no_op_diagnostics.ok() &&
                   first->revision() == first_revision &&
                   first->instructions().front().callee() == *keep,
               "a no-op mapping preserves the Function revision");

  joggle::Diagnostics replace_diagnostics;
  const auto replaced =
      joggle::ir::replace_calls(*first, *keep, *converted, replace_diagnostics);
  ok &= expect(replaced && *replaced == 1U && replace_diagnostics.ok() &&
                   first->revision() != first_revision &&
                   first->instructions().front().callee() == *converted,
               "a committed replacement advances the Function revision");

  const std::string before_invalid = joggle::format(*second, "second");
  const auto before_invalid_revision = second->revision();
  joggle::Diagnostics invalid_diagnostics;
  const auto invalid =
      joggle::ir::replace_calls(*second, *other, *binary, invalid_diagnostics);
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   second->revision() == before_invalid_revision &&
                   joggle::format(*second, "second") == before_invalid,
               "an invalid replacement restores content and revision");

  auto program_first = compiler.function("mapping.first");
  auto program_second = compiler.function("mapping.second");
  joggle::ir::Program program;
  joggle::Diagnostics insertion_diagnostics;
  if (!program_first || !program_second ||
      !program.insert("first", std::move(*program_first),
                      insertion_diagnostics) ||
      !program.insert("second", std::move(*program_second),
                      insertion_diagnostics)) {
    insertion_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto* original_first =
      static_cast<const joggle::ir::Program&>(program).function("first");
  const auto* original_second =
      static_cast<const joggle::ir::Program&>(program).function("second");
  joggle::Diagnostics program_no_op_diagnostics;
  const auto program_no_op = joggle::ir::map_calls(
      program,
      [](const joggle::ir::Instruction&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      program_no_op_diagnostics);
  ok &= expect(
      program_no_op && *program_no_op == 0U && program_no_op_diagnostics.ok() &&
          static_cast<const joggle::ir::Program&>(program).function("first") ==
              original_first &&
          static_cast<const joggle::ir::Program&>(program).function("second") ==
              original_second,
      "a no-op Program mapping preserves shared Function storage");

  joggle::Diagnostics program_failure_diagnostics;
  const auto program_failure = joggle::ir::map_calls(
      program,
      [&](const joggle::ir::Instruction& instruction)
          -> std::optional<joggle::Module::FunctionDecl> {
        if (instruction.callee() == *keep) {
          return *converted;
        }
        if (instruction.callee() == *other) {
          return *binary;
        }
        return std::nullopt;
      },
      program_failure_diagnostics);
  const auto* unchanged_first =
      static_cast<const joggle::ir::Program&>(program).function("first");
  const auto* unchanged_second =
      static_cast<const joggle::ir::Program&>(program).function("second");
  ok &= expect(
      !program_failure && !program_failure_diagnostics.ok() &&
          unchanged_first != nullptr && unchanged_second != nullptr &&
          unchanged_first->instructions().front().callee() == *keep &&
          unchanged_second->instructions().front().callee() == *other,
      "a failed whole-Program mapping publishes no partial Function edits");

  joggle::Diagnostics program_success_diagnostics;
  const auto program_success = joggle::ir::replace_calls(
      program, *keep, *converted, program_success_diagnostics);
  const auto* mapped_first =
      static_cast<const joggle::ir::Program&>(program).function("first");
  const auto* preserved_second =
      static_cast<const joggle::ir::Program&>(program).function("second");
  ok &=
      expect(program_success && *program_success == 1U &&
                 program_success_diagnostics.ok() && mapped_first != nullptr &&
                 mapped_first != original_first &&
                 mapped_first->instructions().front().callee() == *converted &&
                 preserved_second == original_second,
             "whole-Program replacement detaches only changed Functions");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
