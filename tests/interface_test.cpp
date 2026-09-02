#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

constexpr std::string_view source = R"(
joggle 1;

module semantics@1.0.0 {

  interface metric: type {
    score(scale: i64) -> i64;
    scale_all(values: list<i64>, scale: i64) -> list<i64>;
    valid() -> bool;
  }
  interface tagged: attr {
    tag() -> string;
  }
  interface costed: op {
    latency() -> i64;
  }

  type scalar(bits: i64 = 7) : metric;
  attr label(name: string = "label") : tagged;
  op work<T: type>() -> T : costed;
  op plain<T: type>() -> T;
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

struct Fixture {
  joggle::Compiler compiler;
  std::optional<joggle::Module> module;

  Fixture() {
    compiler.add(source, "semantics.joggle");
    if (compiler.link()) {
      module = compiler.module("semantics");
    }
  }
};

}  // namespace

int main() {
  Fixture fixture;
  if (!fixture.module) {
    fixture.compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto scalar_decl = fixture.module->type("scalar");
  const auto label_decl = fixture.module->attribute("label");
  const auto work_decl = fixture.module->operation("work");
  const auto metric = fixture.module->interface("metric");
  const auto tagged = fixture.module->interface("tagged");
  const auto costed = fixture.module->interface("costed");
  const auto score = metric ? metric->method("score") : std::nullopt;
  const auto scale_all = metric ? metric->method("scale_all") : std::nullopt;
  const auto valid = metric ? metric->method("valid") : std::nullopt;
  const auto tag = tagged ? tagged->method("tag") : std::nullopt;
  const auto latency = costed ? costed->method("latency") : std::nullopt;
  if (!scalar_decl || !label_decl || !work_decl || !score || !scale_all ||
      !valid || !tag || !latency) {
    return EXIT_FAILURE;
  }

  fixture.compiler.bind(
      *scalar_decl, "score",
      [](const joggle::Type& type, std::int64_t scale,
         joggle::Diagnostics&) -> std::optional<std::int64_t> {
        const auto bits = type.get<std::int64_t>("bits");
        return bits ? std::optional<std::int64_t>{*bits * scale} : std::nullopt;
      });
  fixture.compiler.bind(
      *label_decl, "tag",
      [](const joggle::Attribute& attribute, joggle::Diagnostics&) {
        return attribute.get<std::string>("name");
      });
  fixture.compiler.bind(*scalar_decl, "scale_all",
                        [](const joggle::Type&,
                           std::vector<std::int64_t> values, std::int64_t scale,
                           joggle::Diagnostics&) {
                          for (std::int64_t& value : values) {
                            value *= scale;
                          }
                          return values;
                        });
  fixture.compiler.bind(
      *scalar_decl, "valid",
      [](const joggle::Type&, joggle::Diagnostics&) { return true; });
  fixture.compiler.bind(*work_decl, "latency",
                        [](const joggle::Operation&,
                           joggle::Diagnostics&) -> std::int64_t { return 2; });

  const auto scalar = fixture.compiler.make(*scalar_decl);
  const auto label = fixture.compiler.make(*label_decl);
  auto graph = fixture.compiler.graph();
  if (!scalar || !label || !graph) {
    fixture.compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  auto edit = graph->edit();
  const auto work = edit.append(*work_decl, {}, {*scalar});
  joggle::Diagnostics edit_diagnostics;
  if (!edit.commit(edit_diagnostics)) {
    edit_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto score_result =
      fixture.compiler.call<std::int64_t>(*scalar, "score", std::int64_t{3});
  const auto tag_result = fixture.compiler.call<std::string>(*label, "tag");
  const auto latency_result =
      fixture.compiler.call<std::int64_t>(work, "latency");
  const std::array<std::int64_t, 3> values{1, 2, 3};
  const auto scaled = fixture.compiler.call<std::vector<std::int64_t>>(
      *scalar, "scale_all", values, 4);
  const auto valid_result = fixture.compiler.call<bool>(*scalar, "valid");
  bool ok = true;
  ok &= expect(score_result && *score_result == 21,
               "typed type method validates and receives arguments");
  ok &= expect(tag_result && *tag_result == "label",
               "typed attribute method dispatches");
  ok &= expect(latency_result && *latency_result == 2,
               "typed operation method dispatches");
  ok &= expect(scaled && *scaled == std::vector<std::int64_t>({4, 8, 12}),
               "typed list method arguments and results round-trip");
  ok &= expect(valid_result && *valid_result,
               "a bool method path is distinct from a type verifier");

  Fixture wrong_result;
  const auto wrong_module = wrong_result.module;
  const auto wrong_scalar =
      wrong_module ? wrong_module->type("scalar") : std::nullopt;
  const auto wrong_metric =
      wrong_module ? wrong_module->interface("metric") : std::nullopt;
  const auto wrong_score =
      wrong_metric ? wrong_metric->method("score") : std::nullopt;
  if (!wrong_scalar || !wrong_score) {
    return EXIT_FAILURE;
  }
  wrong_result.compiler.bind(
      *wrong_scalar, *wrong_score,
      [](const joggle::Type&, std::int64_t) { return true; });
  ok &= expect(!wrong_result.compiler.ok(),
               "method result kind mismatch is rejected");

  Fixture wrong_inferred;
  const auto wrong_inferred_module = wrong_inferred.module;
  const auto wrong_inferred_scalar = wrong_inferred_module
                                         ? wrong_inferred_module->type("scalar")
                                         : std::nullopt;
  const auto wrong_inferred_metric =
      wrong_inferred_module ? wrong_inferred_module->interface("metric")
                            : std::nullopt;
  const auto wrong_inferred_score = wrong_inferred_metric
                                        ? wrong_inferred_metric->method("score")
                                        : std::nullopt;
  if (!wrong_inferred_scalar || !wrong_inferred_score) {
    return EXIT_FAILURE;
  }
  wrong_inferred.compiler.bind(
      *wrong_inferred_scalar, *wrong_inferred_score,
      [](const joggle::Type&, double, joggle::Diagnostics&) {
        return std::int64_t{1};
      });
  ok &= expect(!wrong_inferred.compiler.ok(),
               "inferred callable kinds are checked against the Module");

  Fixture diagnosed;
  const auto diagnosed_module = diagnosed.module;
  const auto diagnosed_scalar =
      diagnosed_module ? diagnosed_module->type("scalar") : std::nullopt;
  const auto diagnosed_metric =
      diagnosed_module ? diagnosed_module->interface("metric") : std::nullopt;
  const auto diagnosed_score =
      diagnosed_metric ? diagnosed_metric->method("score") : std::nullopt;
  if (!diagnosed_scalar || !diagnosed_score) {
    return EXIT_FAILURE;
  }
  diagnosed.compiler.bind<std::int64_t, std::int64_t>(
      *diagnosed_scalar, *diagnosed_score,
      [](const joggle::Type&, std::int64_t, joggle::Diagnostics& diagnostics) {
        diagnostics.report("method rejected its subject");
        return std::int64_t{1};
      });
  const auto diagnosed_type = diagnosed.compiler.make(*diagnosed_scalar);
  const auto diagnosed_result =
      diagnosed_type ? diagnosed.compiler.call<std::int64_t>(
                           *diagnosed_type, *diagnosed_score, std::int64_t{1})
                     : std::nullopt;
  ok &= expect(!diagnosed_result && !diagnosed.compiler.ok(),
               "a method diagnostic suppresses its returned value");

  Fixture missing_method;
  const auto missing_scalar = missing_method.module->type("scalar");
  if (!missing_scalar) {
    return EXIT_FAILURE;
  }
  missing_method.compiler.bind(*missing_scalar, "missing",
                               [](const joggle::Type&,
                                  joggle::Diagnostics&) {
                                 return std::int64_t{0};
                               });
  const auto method_diagnostics =
      missing_method.compiler.diagnostics().entries();
  ok &= expect(!missing_method.compiler.ok() && !method_diagnostics.empty() &&
                   method_diagnostics.back().message.find(
                       "has no interface method named 'missing'") !=
                       std::string::npos,
               "named method binding reports an unresolved method");

  constexpr std::string_view ambiguous_source = R"(
joggle 1;
module ambiguous@1.0.0 {
  interface first: type {
    value() -> i64;
  }
  interface second: type {
    value() -> i64;
  }
  type both() : first, second;
}
)";
  joggle::Compiler ambiguous;
  ambiguous.add(ambiguous_source, "ambiguous.joggle");
  const bool ambiguous_linked = ambiguous.link();
  const auto ambiguous_module = ambiguous.module("ambiguous");
  if (!ambiguous_linked || !ambiguous_module) {
    return EXIT_FAILURE;
  }
  const auto ambiguous_both = ambiguous_module->type("both");
  if (!ambiguous_both) {
    return EXIT_FAILURE;
  }
  ambiguous.bind(*ambiguous_both, "value",
                 [](const joggle::Type&, joggle::Diagnostics&) {
                   return std::int64_t{1};
                 });
  const auto ambiguous_diagnostics = ambiguous.diagnostics().entries();
  ok &= expect(!ambiguous.ok() && !ambiguous_diagnostics.empty() &&
                   ambiguous_diagnostics.back().message.find("ambiguous") !=
                       std::string::npos &&
                   ambiguous_diagnostics.back().message.find(
                       "both.first.value") != std::string::npos &&
                   ambiguous_diagnostics.back().message.find(
                       "both.second.value") != std::string::npos,
               "a short method path reports precise qualification choices");

  joggle::Compiler qualified;
  qualified.add(ambiguous_source, "ambiguous.joggle");
  const bool qualified_linked = qualified.link();
  const auto qualified_module = qualified.module("ambiguous");
  if (!qualified_linked || !qualified_module) {
    return EXIT_FAILURE;
  }
  const auto qualified_both = qualified_module->type("both");
  if (!qualified_both) {
    return EXIT_FAILURE;
  }
  qualified.bind(*qualified_both, "first.value",
                 [](const joggle::Type&, joggle::Diagnostics&) {
                   return std::int64_t{1};
                 });
  ok &= expect(qualified.ok(),
               "an explicit interface path resolves a real name collision");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
