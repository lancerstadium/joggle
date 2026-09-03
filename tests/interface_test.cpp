#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
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

constexpr std::string_view source = R"(
joggle 1;

module semantics@1.0.0 {
  interface metric: type {
    score: int;
    valid: bool;
  }
  interface tagged: attr {
    tag() -> string;
    crash() -> string;
  }
  interface costed: fn {
    latency() -> int;
    crash() -> int;
  }

  type scalar(bits: int = 7) : metric {
    score = bits * 3;
    valid = true;
  }
  attr label(name: string = "label") : tagged;
  fn work<T: type>() -> T : costed;
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.add(source, "semantics.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto module = compiler.module("semantics");
  const auto scalar_decl = module ? module->type("scalar") : std::nullopt;
  const auto label_decl = module ? module->attribute("label") : std::nullopt;
  const auto work_decl = module ? module->function("work") : std::nullopt;
  const auto metric = module ? module->interface("metric") : std::nullopt;
  const auto tagged = module ? module->interface("tagged") : std::nullopt;
  const auto costed = module ? module->interface("costed") : std::nullopt;
  const auto tag = tagged ? tagged->method("tag") : std::nullopt;
  const auto latency = costed ? costed->method("latency") : std::nullopt;
  const auto attribute_crash = tagged ? tagged->method("crash") : std::nullopt;
  const auto instruction_crash =
      costed ? costed->method("crash") : std::nullopt;
  if (!scalar_decl || !label_decl || !work_decl || !metric || !tag ||
      !latency || !attribute_crash || !instruction_crash) {
    return EXIT_FAILURE;
  }

  compiler.bind(*label_decl, *tag, [](const joggle::Attribute& attribute) {
    return attribute.get<std::string>("name");
  });
  compiler.bind(*work_decl, *latency,
                [](const joggle::Instruction&) -> std::int64_t { return 2; });
  compiler.bind(*label_decl, *attribute_crash,
                [](const joggle::Attribute&) -> std::string {
                  throw std::runtime_error("attribute method exception");
                });
  compiler.bind(*work_decl, *instruction_crash,
                [](const joggle::Instruction&) -> std::int64_t { throw 1; });

  const auto scalar = compiler.make(*scalar_decl);
  const auto label = compiler.make(*label_decl);
  auto function = compiler.create_function();
  if (!scalar || !label || !function) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  auto edit = function->edit();
  const auto work = edit.append(*work_decl, {}, {*scalar});
  joggle::Diagnostics edit_diagnostics;
  if (!edit.commit(edit_diagnostics)) {
    return EXIT_FAILURE;
  }

  const auto tag_result = compiler.call<std::string>(*label, *tag);
  const auto latency_result = compiler.call<std::int64_t>(work, *latency);
  bool ok = true;
  ok &= expect(metric->fields().size() == 2U && metric->methods().empty(),
               "type interfaces expose fields rather than methods");
  ok &= expect(scalar->get<std::int64_t>("score") ==
                       std::optional<std::int64_t>{21} &&
                   scalar->get<bool>("valid") == std::optional<bool>{true},
               "derived parameters share Type::get with identity parameters");
  ok &= expect(tag_result == std::optional<std::string>{"label"} &&
                   latency_result == std::optional<std::int64_t>{2},
               "attribute and function-call behavior methods remain typed");
  const auto rejected_attribute_method =
      compiler.call<std::string>(*label, *attribute_crash);
  const auto rejected_instruction_method =
      compiler.call<std::int64_t>(work, *instruction_crash);
  const auto thrown_methods = std::count_if(
      compiler.diagnostics().entries().begin(),
      compiler.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("interface method") !=
                   std::string::npos &&
               diagnostic.message.find("threw") != std::string::npos;
      });
  ok &= expect(!rejected_attribute_method && !rejected_instruction_method &&
                   thrown_methods == 2,
               "interface method exceptions become ordinary diagnostics");

  joggle::Compiler ambiguous;
  ambiguous.add(R"(
joggle 1;
module ambiguous@1.0.0 {
  interface first: type { value: int; }
  interface second: type { value: int; }
  type both() : first, second { value = 1; }
}
)",
                "ambiguous.joggle");
  ok &= expect(!ambiguous.link(),
               "a derived parameter must name exactly one interface field");

  joggle::Compiler recursive;
  recursive.add(R"(
joggle 1;
module recursive@1.0.0 {
  interface linked: type { next: type; }
  type cycle() : linked { next = cycle; }
}
)",
                "recursive.joggle");
  const bool recursive_linked = recursive.link();
  const auto recursive_module = recursive.module("recursive");
  const auto cycle_decl =
      recursive_module ? recursive_module->type("cycle") : std::nullopt;
  const auto cycle = cycle_decl ? recursive.make(*cycle_decl) : std::nullopt;
  ok &= expect(recursive_linked && !cycle && !recursive.ok(),
               "recursive derived type construction is diagnosed");

  joggle::Compiler generic_fields;
  generic_fields.add(R"(
joggle 1;
module field_contracts@1.0.0 {
  interface measured: type { units: int; }
}
)",
                     "field-contracts.joggle");
  generic_fields.add(R"(
joggle 1;
module field_queries@1.0.0 {
  import field_contracts@1 as fields;
  fn units<T: fields.measured>() -> int {
    return T.units;
  }
}
)",
                     "field-queries.joggle");
  ok &= expect(generic_fields.link(),
               "a compiler function can read a field promised by an "
               "imported type interface");

  joggle::Compiler invalid_constraint;
  invalid_constraint.add(R"(
joggle 1;
module behavior_contracts@1.0.0 {
  interface executable: fn;
}
)",
                         "behavior-contracts.joggle");
  invalid_constraint.add(R"(
joggle 1;
module invalid_query@1.0.0 {
  import behavior_contracts@1 as behavior;
  fn invalid<T: behavior.executable>() -> int {
    return 0;
  }
}
)",
                         "invalid-query.joggle");
  const bool invalid_linked = invalid_constraint.link();
  const bool reports_non_type_constraint =
      std::any_of(invalid_constraint.diagnostics().entries().begin(),
                  invalid_constraint.diagnostics().entries().end(),
                  [](const joggle::Diagnostic& diagnostic) {
                    return diagnostic.message.find("non-type interface") !=
                           std::string::npos;
                  });
  ok &= expect(!invalid_linked && reports_non_type_constraint,
               "an imported generic constraint is checked even when the "
               "function has no module values");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
