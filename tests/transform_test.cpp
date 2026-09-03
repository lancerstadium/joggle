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

  const auto schema = compiler.module("mapping");
  const auto keep = schema ? schema->declaration("keep") : std::nullopt;
  const auto converted =
      schema ? schema->declaration("converted") : std::nullopt;
  const auto other = schema ? schema->declaration("other") : std::nullopt;
  const auto binary = schema ? schema->declaration("binary") : std::nullopt;
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

  auto module_first = compiler.function("mapping.first");
  auto module_second = compiler.function("mapping.second");
  joggle::Module module("mapping_result", {1, 0, 0});
  joggle::Diagnostics insertion_diagnostics;
  if (!module_first || !module_second ||
      !module.insert("first", std::move(*module_first),
                     insertion_diagnostics) ||
      !module.insert("second", std::move(*module_second),
                     insertion_diagnostics)) {
    insertion_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto* original_first =
      static_cast<const joggle::Module&>(module).function("first");
  const auto* original_second =
      static_cast<const joggle::Module&>(module).function("second");
  const auto original_first_revision = original_first->revision();
  const auto original_second_revision = original_second->revision();
  const std::string original_digest(module.digest());
  joggle::Diagnostics module_no_op_diagnostics;
  const auto module_no_op = joggle::ir::map_calls(
      module,
      [](const joggle::ir::Instruction&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      module_no_op_diagnostics);
  ok &= expect(
      module_no_op && *module_no_op == 0U && module_no_op_diagnostics.ok() &&
          module.digest() == original_digest &&
          static_cast<const joggle::Module&>(module).function("first") ==
              original_first &&
          static_cast<const joggle::Module&>(module).function("second") ==
              original_second,
      "a no-op Module mapping preserves shared Function storage");

  joggle::Diagnostics module_failure_diagnostics;
  const auto module_failure = joggle::ir::map_calls(
      module,
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
      module_failure_diagnostics);
  const auto* unchanged_first =
      static_cast<const joggle::Module&>(module).function("first");
  const auto* unchanged_second =
      static_cast<const joggle::Module&>(module).function("second");
  ok &= expect(
      !module_failure && !module_failure_diagnostics.ok() &&
          module.digest() == original_digest && unchanged_first != nullptr &&
          unchanged_second != nullptr &&
          unchanged_first->instructions().front().callee() == *keep &&
          unchanged_second->instructions().front().callee() == *other,
      "a failed whole-Module mapping publishes no partial Function edits");

  joggle::Diagnostics module_success_diagnostics;
  const auto module_success = joggle::ir::replace_calls(
      module, *keep, *converted, module_success_diagnostics);
  const auto* mapped_first =
      static_cast<const joggle::Module&>(module).function("first");
  const auto* preserved_second =
      static_cast<const joggle::Module&>(module).function("second");
  ok &=
      expect(module_success && *module_success == 1U &&
                 module_success_diagnostics.ok() && mapped_first != nullptr &&
                 module.digest() != original_digest &&
                 mapped_first->revision() != original_first_revision &&
                 mapped_first->instructions().front().callee() == *converted &&
                 preserved_second != nullptr &&
                 preserved_second->revision() == original_second_revision,
             "whole-Module replacement advances only changed revisions");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
