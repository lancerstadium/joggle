#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
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

  fn expanded(input: word) -> word {
    return keep(input);
  }
}
)",
               "mapping.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto schema = compiler.module("mapping");
  const auto keep = schema ? schema->function("keep") : std::nullopt;
  const auto converted = schema ? schema->function("converted") : std::nullopt;
  const auto other = schema ? schema->function("other") : std::nullopt;
  const auto binary = schema ? schema->function("binary") : std::nullopt;
  auto first = compiler.materialize("mapping.first");
  auto second = compiler.materialize("mapping.second");
  auto expanded = compiler.materialize("mapping.expanded");
  auto convertible = compiler.materialize("mapping.first");
  auto fixedpoint = compiler.materialize("mapping.expanded");
  auto oscillating = compiler.materialize("mapping.expanded");
  if (!keep || !converted || !other || !binary || !first || !second ||
      !expanded || !convertible || !fixedpoint || !oscillating) {
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto first_revision = first->revision();
  joggle::Diagnostics no_op_diagnostics;
  const auto no_op = joggle::map_calls(
      *first,
      [](const joggle::Instruction&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      no_op_diagnostics);
  ok &= expect(no_op && *no_op == 0U && no_op_diagnostics.ok() &&
                   first->revision() == first_revision &&
                   first->instructions().front().callee() == *keep,
               "a no-op mapping preserves the Function revision");

  joggle::Diagnostics no_op_rewrite_diagnostics;
  const auto no_op_rewrite = joggle::rewrite(
      *first,
      [](const joggle::Instruction&, joggle::Function::Edit&,
         joggle::Diagnostics&) { return false; },
      no_op_rewrite_diagnostics);
  ok &= expect(no_op_rewrite && *no_op_rewrite == 0U &&
                   no_op_rewrite_diagnostics.ok() &&
                   first->revision() == first_revision,
               "a no-op rewrite preserves the Function revision");

  joggle::Diagnostics replace_diagnostics;
  const auto replaced =
      joggle::replace_calls(*first, *keep, *converted, replace_diagnostics);
  ok &= expect(replaced && *replaced == 1U && replace_diagnostics.ok() &&
                   first->revision() != first_revision &&
                   first->instructions().front().callee() == *converted,
               "a committed replacement advances the Function revision");

  const auto expanded_revision = expanded->revision();
  joggle::Diagnostics rewrite_diagnostics;
  const auto rewritten = joggle::rewrite(
      *expanded,
      [&](const joggle::Instruction& instruction, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (instruction.callee() != *keep) {
          return false;
        }
        const auto first_step =
            edit.insert(instruction, *converted, instruction.arguments());
        const auto second_step =
            edit.insert(instruction, *other, {first_step.value()});
        edit.replace(instruction, {second_step.value()});
        return true;
      },
      rewrite_diagnostics);
  const auto expanded_instructions = expanded->instructions();
  ok &= expect(
      rewritten && *rewritten == 1U && rewrite_diagnostics.ok() &&
          expanded->revision() != expanded_revision &&
          expanded_instructions.size() == 2U &&
          expanded_instructions[0].callee() == *converted &&
          expanded_instructions[1].callee() == *other &&
          expanded->entry().terminator().returned().front() ==
              expanded_instructions[1].value(),
      "one lambda transactionally expands a call into multiple Instructions");

  const auto convertible_revision = convertible->revision();
  joggle::Diagnostics conversion_diagnostics;
  const auto conversion = joggle::convert(
      *convertible,
      [&](const joggle::Instruction& instruction, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (instruction.callee() != *keep) {
          return false;
        }
        edit.replace(instruction, *converted);
        return true;
      },
      [&](const joggle::Instruction& instruction) {
        return instruction.callee() != *keep;
      },
      conversion_diagnostics);
  ok &= expect(conversion && *conversion == 1U && conversion_diagnostics.ok() &&
                   convertible->revision() != convertible_revision &&
                   convertible->instructions().front().callee() == *converted,
               "conversion publishes a rewritten legal Function");

  const auto staged_rewrite = [&](const joggle::Instruction& instruction,
                                  joggle::Function::Edit& edit,
                                  joggle::Diagnostics&) {
    if (instruction.callee() == *keep) {
      edit.replace(instruction, *converted);
      return true;
    }
    if (instruction.callee() == *converted) {
      edit.replace(instruction, *other);
      return true;
    }
    return false;
  };
  joggle::Diagnostics fixedpoint_diagnostics;
  const auto fixedpoint_changes = joggle::rewrite_to_fixpoint(
      *fixedpoint, staged_rewrite, 3U, fixedpoint_diagnostics);
  ok &= expect(fixedpoint_changes && *fixedpoint_changes == 2U &&
                   fixedpoint_diagnostics.ok() &&
                   fixedpoint->instructions().front().callee() == *other,
               "bounded sweeps process calls inserted by an earlier sweep");

  const auto oscillating_revision = oscillating->revision();
  joggle::Diagnostics oscillating_diagnostics;
  const auto oscillating_result = joggle::rewrite_to_fixpoint(
      *oscillating,
      [&](const joggle::Instruction& instruction, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (instruction.callee() == *keep) {
          edit.replace(instruction, *converted);
        } else {
          edit.replace(instruction, *keep);
        }
        return true;
      },
      2U, oscillating_diagnostics);
  ok &= expect(!oscillating_result && !oscillating_diagnostics.ok() &&
                   oscillating->revision() == oscillating_revision &&
                   oscillating->instructions().front().callee() == *keep,
               "a non-convergent rewrite publishes no intermediate sweep");

  const std::string before_invalid = joggle::format(*second, "second");
  const auto before_invalid_revision = second->revision();
  joggle::Diagnostics invalid_diagnostics;
  const auto invalid =
      joggle::replace_calls(*second, *other, *binary, invalid_diagnostics);
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   second->revision() == before_invalid_revision &&
                   joggle::format(*second, "second") == before_invalid,
               "an invalid replacement restores content and revision");

  auto module_first = compiler.materialize("mapping.first");
  auto module_second = compiler.materialize("mapping.second");
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

  const auto read_body = [&](std::string_view name) {
    const auto function =
        static_cast<const joggle::Module&>(module).function(name);
    return function ? function->body() : nullptr;
  };
  const auto* original_first = read_body("first");
  const auto* original_second = read_body("second");
  const auto original_first_revision = original_first->revision();
  const auto original_second_revision = original_second->revision();
  const std::string original_digest(module.digest());

  joggle::Module fixedpoint_module = module;
  joggle::Diagnostics fixedpoint_module_diagnostics;
  const auto fixedpoint_module_changes = joggle::rewrite_to_fixpoint(
      fixedpoint_module, staged_rewrite, 3U, fixedpoint_module_diagnostics);
  const auto fixedpoint_first = fixedpoint_module.function("first");
  ok &= expect(fixedpoint_module_changes && *fixedpoint_module_changes == 2U &&
                   fixedpoint_module_diagnostics.ok() && fixedpoint_first &&
                   fixedpoint_first->body() != nullptr &&
                   fixedpoint_first->body()->instructions().front().callee() ==
                       *other &&
                   module.digest() == original_digest,
               "fixed-point Module rewriting publishes one final value");

  joggle::Diagnostics module_no_op_diagnostics;
  const auto module_no_op = joggle::map_calls(
      module,
      [](const joggle::Instruction&)
          -> std::optional<joggle::Module::FunctionDecl> {
        return std::nullopt;
      },
      module_no_op_diagnostics);
  ok &= expect(module_no_op && *module_no_op == 0U &&
                   module_no_op_diagnostics.ok() &&
                   module.digest() == original_digest &&
                   read_body("first") == original_first &&
                   read_body("second") == original_second,
               "a no-op Module mapping preserves shared Function storage");

  joggle::Diagnostics module_failure_diagnostics;
  const auto module_failure = joggle::convert(
      module,
      [&](const joggle::Instruction& instruction, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (instruction.callee() == *keep) {
          edit.replace(instruction, *converted);
          return true;
        }
        return false;
      },
      [&](const joggle::Instruction& instruction) {
        return instruction.callee() != *other;
      },
      module_failure_diagnostics);
  const auto* unchanged_first = read_body("first");
  const auto* unchanged_second = read_body("second");
  ok &= expect(!module_failure && !module_failure_diagnostics.ok() &&
                   module_failure_diagnostics.entries().front().message.find(
                       "function 'second'") != std::string::npos &&
                   module.digest() == original_digest &&
                   unchanged_first != nullptr && unchanged_second != nullptr &&
                   unchanged_first->instructions().front().callee() == *keep &&
                   unchanged_second->instructions().front().callee() == *other,
               "an illegal whole-Module conversion publishes no partial edits");

  joggle::Diagnostics module_success_diagnostics;
  const auto module_success = joggle::replace_calls(module, *keep, *converted,
                                                    module_success_diagnostics);
  const auto* mapped_first = read_body("first");
  const auto* preserved_second = read_body("second");
  const auto mapped_declaration = module.function("first");
  const std::string mapped_symbol_digest =
      mapped_declaration
          ? std::string(mapped_declaration->symbol().module_digest())
          : std::string{};
  ok &=
      expect(module_success && *module_success == 1U &&
                 module_success_diagnostics.ok() && mapped_first != nullptr &&
                 module.digest() != original_digest &&
                 mapped_symbol_digest == module.digest() &&
                 mapped_first->revision() != original_first_revision &&
                 mapped_first->instructions().front().callee() == *converted &&
                 preserved_second != nullptr &&
                 preserved_second->revision() == original_second_revision,
             "whole-Module replacement advances only changed revisions and "
             "publishes one current Module identity");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
