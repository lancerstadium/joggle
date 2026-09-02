#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
  std::ifstream input(JOGGLE_TEST_MODULE);
  std::ostringstream text;
  text << input.rdbuf();

  joggle::Diagnostics diagnostics;
  auto module =
      joggle::parse_module(text.str(), diagnostics, JOGGLE_TEST_MODULE);
  if (!module) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  ok &= expect(module->name() == "arith", "module name");
  ok &= expect(module->digest().size() == 64U, "SHA-256 digest width");

  const auto rejects_language_version = [](std::uint32_t version) {
    joggle::Diagnostics diagnostics;
    const auto parsed = joggle::parse_module(
        "joggle " + std::to_string(version) +
            "; module versioned@1.0.0 {}",
        diagnostics, "language-version.joggle");
    return !parsed && !diagnostics.ok() &&
           diagnostics.entries().front().message.find(
               "unsupported Joggle language version") != std::string::npos &&
           diagnostics.entries().front().source &&
           diagnostics.entries().front().source->begin.line == 1U;
  };
  ok &= expect(rejects_language_version(0U) &&
                   rejects_language_version(2U),
               "unknown language versions are rejected before Module use");

  const auto integer = module->type("integer");
  const auto numeric_format = module->interface("numeric_format");
  const auto elementwise = module->interface("elementwise");
  const auto add = module->operation("add");
  const auto canonicalize = module->pass("canonicalize");
  ok &= expect(integer && integer->parameters().size() == 2U &&
                   integer->interfaces().size() == 1U,
               "integer schema");
  const auto storage_bits =
      numeric_format ? numeric_format->method("storage_bits") : std::nullopt;
  ok &= expect(
      numeric_format &&
          numeric_format->subject() == joggle::Module::SymbolKind::Type &&
          numeric_format->methods().size() == 2U && storage_bits &&
          storage_bits->result_kind() == joggle::Module::ParameterKind::I64 &&
          storage_bits->stable_name().ends_with(
              "/interface/numeric_format/method/storage_bits"),
      "interface method reflection");
  ok &= expect(elementwise && elementwise->methods().empty(),
               "marker interface reflection");
  ok &=
      expect(add && add->inputs().size() == 2U && add->results().size() == 1U &&
                 add->interfaces().size() == 2U,
             "add signature");
  ok &= expect(canonicalize && canonicalize->form() ==
                                   joggle::Module::PassDecl::Form::Rules,
               "rule pass declaration");
  if (!ok || !integer) {
    return EXIT_FAILURE;
  }

  const auto symbol = integer->symbol();
  ok &= expect(symbol.qualified_name() == "arith.integer", "qualified symbol");
  ok &= expect(symbol.stable_name().starts_with("arith@1.0.0#") &&
                   symbol.stable_name().ends_with("/type/integer"),
               "persistent symbol identity");

  const std::string canonical = joggle::format(*module);
  ok &= expect(canonical == text.str(),
               "the shipped arith module is canonical source");
  joggle::Diagnostics canonical_diagnostics;
  auto reparsed = joggle::parse_module(canonical, canonical_diagnostics,
                                       "canonical.joggle");
  ok &= expect(reparsed && joggle::format(*reparsed) == canonical,
               "idempotent formatting");
  ok &= expect(reparsed && reparsed->digest() == module->digest(),
               "canonical digest stability");
  const auto reparsed_integer = reparsed ? reparsed->type("integer") : std::nullopt;

  joggle::Diagnostics surface_diagnostics;
  const auto surface = joggle::parse_module(R"(
    joggle 1;
    module surface@2.3.4 {
      graph empty() {
        return;
      }
      pass pipeline = external;
      pass external;
      op collect<T: type>(items: T...) -> T : marker;
      attr meta(values: list<i64>, element: type);
      type scalar(
        bits: i64 = 8,
        scale: f64 = -1.5e2,
        signed: bool = true,
        label: string = "line\n\"quoted\""
      ) : metric;
      interface marker: op;
      interface metric: type {
        shape(axis: i64) -> list<i64>;
      }
      import dependency@^1.2.3 as dep;
    }
  )",
                                            surface_diagnostics,
                                            "surface.joggle");
  const std::string surface_text = surface ? joggle::format(*surface) : "";
  const auto position = [&](std::string_view text) {
    return surface_text.find(text);
  };
  const auto import_position = position("import dependency");
  const auto interface_position = position("interface marker");
  const auto type_position = position("type scalar");
  const auto attribute_position = position("attr meta");
  const auto operation_position = position("op collect");
  const auto pass_position = position("pass pipeline");
  const auto graph_position = position("graph empty");
  joggle::Diagnostics surface_roundtrip_diagnostics;
  const auto surface_roundtrip = joggle::parse_module(
      surface_text, surface_roundtrip_diagnostics, "surface-canonical.joggle");
  ok &= expect(
      surface && import_position < interface_position &&
          interface_position < type_position &&
          type_position < attribute_position &&
          attribute_position < operation_position &&
          operation_position < pass_position &&
          pass_position < graph_position && surface_roundtrip &&
          joggle::format(*surface_roundtrip) == surface_text &&
          surface_roundtrip->digest() == surface->digest(),
      "the complete DSL surface has one canonical member order and digest");

  joggle::Diagnostics trivia_diagnostics;
  const std::string with_trivia =
      "\n// source location and comments are not identity\n" + text.str() +
      "\n# trailing package note\n";
  const auto trivia_module = joggle::parse_module(
      with_trivia, trivia_diagnostics, "another/path/arith.joggle");
  const auto trivia_integer =
      trivia_module ? trivia_module->type("integer") : std::nullopt;
  ok &= expect(trivia_module && trivia_integer &&
                   trivia_module->digest() == module->digest() &&
                   joggle::format(*trivia_module) == canonical &&
                   trivia_integer->symbol() == integer->symbol(),
               "whitespace, comments, and source paths do not affect "
               "canonical identity");

  joggle::Diagnostics conflicting_identity_diagnostics;
  const auto conflicting_identity = joggle::parse_module(
      R"(
    joggle 1;
    module arith@1.0.0 {
      type integer(width: i64);
    }
  )",
      conflicting_identity_diagnostics, "conflicting-identity.joggle");
  const auto conflicting_integer =
      conflicting_identity ? conflicting_identity->type("integer") : std::nullopt;
  ok &= expect(reparsed_integer && conflicting_integer && *reparsed_integer == *integer &&
                   *conflicting_integer != *integer,
               "declaration equality includes the canonical module digest");

  joggle::Diagnostics first_graph_diagnostics;
  const auto first_graph_module = joggle::parse_module(R"(
    joggle 1;
    module graph_identity@1.0.0 {
      type value();
      op identity<T: type>(input: T) -> T;
      graph main(%input: value) -> value {
        return %input;
      }
    }
  )",
                                                        first_graph_diagnostics,
                                                        "first-graph.joggle");
  joggle::Diagnostics second_graph_diagnostics;
  const auto second_graph_module = joggle::parse_module(R"(
    joggle 1;
    module graph_identity@1.0.0 {
      type value();
      op identity<T: type>(input: T) -> T;
      graph main(%input: value) -> value {
        %output = identity(%input);
        return %output;
      }
    }
  )",
                                                         second_graph_diagnostics,
                                                         "second-graph.joggle");
  const auto first_graph_symbol =
      first_graph_module
          ? first_graph_module->symbol(joggle::Module::SymbolKind::Graph,
                                       "main")
          : std::nullopt;
  const auto second_graph_symbol =
      second_graph_module
          ? second_graph_module->symbol(joggle::Module::SymbolKind::Graph,
                                        "main")
          : std::nullopt;
  ok &= expect(first_graph_module && second_graph_module && first_graph_symbol &&
                   second_graph_symbol &&
                   first_graph_module->digest() != second_graph_module->digest() &&
                   *first_graph_symbol != *second_graph_symbol,
               "graph body changes alter the Module and graph symbol identity");

  joggle::Diagnostics list_diagnostics;
  auto list_module = joggle::parse_module(R"(
    joggle 1;
    module shaped@1.0.0 {
      type tensor(element: type, shape: list<i64>);
    }
  )",
                                          list_diagnostics, "shaped.joggle");
  const auto tensor = list_module ? list_module->type("tensor") : std::nullopt;
  ok &= expect(tensor && tensor->parameters().size() == 2U &&
                   tensor->parameters()[1].list,
               "list-valued schema parameter");

  joggle::Diagnostics numeric_diagnostics;
  auto numeric_module =
      joggle::parse_module(R"(
    joggle 1;
    module numeric@1.2.3 {
      attr scale(value: f64 = -1.5e2);
    }
  )",
                           numeric_diagnostics, "numeric.joggle");
  const auto scale =
      numeric_module ? numeric_module->attribute("scale") : std::nullopt;
  ok &= expect(scale && scale->parameters().size() == 1U &&
                   scale->parameters().front().default_value &&
                   std::get<double>(
                       *scale->parameters().front().default_value) == -150.0,
               "semantic versions and numeric literals share one lexer");

  joggle::Compiler compiler;
  compiler.add(canonical, "arith.joggle");
  compiler.add(R"(
    joggle 1;
    module client@1.0.0 {
      import arith@1 as math;
      op inference<T: type>(input: T) -> T : math.elementwise;
      pass pipeline = math.canonicalize;
    }
  )",
               "client.joggle");
  ok &= expect(compiler.link(), "module closure links");
  ok &= expect(compiler.linked() && compiler.module("client").has_value(),
               "linked module lookup");
  const auto linked_client = compiler.module("client");
  ok &= expect(linked_client && linked_client->imports().size() == 1U &&
                   linked_client->imports().front().name == "arith" &&
                   linked_client->imports().front().prefix() == "math",
               "an import alias is a local prefix for one module identity");
  const auto inference =
      linked_client ? linked_client->operation("inference") : std::nullopt;
  ok &= expect(inference && elementwise &&
                   compiler.conforms(*inference, *elementwise),
               "aliased cross-module interface implementation");

  joggle::Diagnostics duplicate_alias_diagnostics;
  const auto duplicate_alias = joggle::parse_module(R"(
    joggle 1;
    module aliases@1.0.0 {
      import first@1 as dep;
      import second@1 as dep;
    }
  )",
                                                    duplicate_alias_diagnostics,
                                                    "duplicate-alias.joggle");
  ok &= expect(!duplicate_alias && !duplicate_alias_diagnostics.ok(),
               "duplicate import prefixes are rejected");

  joggle::Diagnostics module_alias_diagnostics;
  const auto module_alias =
      joggle::parse_module(R"(
    joggle 1;
    module aliases@1.0.0 {
      import first@1 as aliases;
    }
  )",
                           module_alias_diagnostics, "module-alias.joggle");
  ok &= expect(!module_alias && !module_alias_diagnostics.ok(),
               "an import prefix cannot shadow its module");

  joggle::Diagnostics invalid_diagnostics;
  auto invalid = joggle::parse_module(
      "joggle 1; module broken@1.0.0 { op add { inputs 2; } }",
      invalid_diagnostics, "broken.joggle");
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   invalid_diagnostics.entries().front().source.has_value(),
               "old clause grammar is rejected with a source diagnostic");

  joggle::Diagnostics interface_diagnostics;
  auto invalid_interface =
      joggle::parse_module(R"(
    joggle 1;
    module broken_interface@1.0.0 {
      type integer() : missing;
    }
  )",
                           interface_diagnostics, "broken-interface.joggle");
  ok &= expect(!invalid_interface && !interface_diagnostics.ok() &&
                   interface_diagnostics.entries().back().source &&
                   interface_diagnostics.entries().back().source->source ==
                       "broken-interface.joggle" &&
                   interface_diagnostics.entries().back().source->begin.line ==
                       4U,
               "unknown local interface is rejected");

  joggle::Diagnostics rule_diagnostics;
  auto invalid_rule =
      joggle::parse_module(R"(
    joggle 1;
    module broken_rule@1.0.0 {
      type integer();
      op identity<T: type>(input: T) -> T;
      pass incomplete {
        identity($input) => $missing;
      }
    }
  )",
                           rule_diagnostics, "broken-rule.joggle");
  ok &= expect(!invalid_rule && !rule_diagnostics.ok() &&
                   rule_diagnostics.entries().back().source &&
                   rule_diagnostics.entries().back().source->source ==
                       "broken-rule.joggle" &&
                   rule_diagnostics.entries().back().source->begin.line == 7U,
               "a replacement outside the matched term is rejected");

  const auto rejects_rule = [&](std::string_view rule) {
    joggle::Diagnostics diagnostics;
    const std::string source = "joggle 1; module invalid_rule@1.0.0 { "
                               "type value(); "
                               "op first<T: type>(input: T) -> T; "
                               "op second<T: type>(input: T) -> T; "
                               "pass invalid { " +
                               std::string(rule) + " } }";
    return !joggle::parse_module(source, diagnostics, "invalid-rule.joggle") &&
           !diagnostics.ok();
  };
  ok &= expect(rejects_rule("first($x) => second($x);"),
               "a contraction cannot construct a new operation");
  ok &= expect(rejects_rule("first($x) => first($x);"),
               "an identity contraction cannot make no progress");
  ok &= expect(rejects_rule("$x => $x;"),
               "a contraction must match an operation root");
  ok &= expect(rejects_rule("first(%x) => %x;"),
               "an SSA name is not accepted as a pass pattern variable");

  joggle::Diagnostics removed_analysis_diagnostics;
  auto removed_analysis = joggle::parse_module(
      R"(
    joggle 1;
    module removed_analysis@1.0.0 {
      analysis widths;
    }
  )",
      removed_analysis_diagnostics, "removed-analysis.joggle");
  ok &= expect(!removed_analysis && !removed_analysis_diagnostics.ok(),
               "removed analysis declaration is rejected");

  joggle::Diagnostics removed_require_diagnostics;
  auto removed_require = joggle::parse_module(
      R"(
    joggle 1;
    module removed_require@1.0.0 {
      require arith;
    }
  )",
      removed_require_diagnostics, "removed-require.joggle");
  ok &= expect(!removed_require && !removed_require_diagnostics.ok(),
               "removed require declaration is rejected");

  joggle::Diagnostics value_name_diagnostics;
  const auto value_name =
      joggle::parse_module(R"(
    joggle 1;
    module value_name@1.0.0 {
      type value();
      op identity(input: value) -> value;
    }
  )",
                           value_name_diagnostics, "value-name.joggle");
  ok &= expect(value_name && value_name_diagnostics.ok(),
               "value is an ordinary type name rather than dead syntax");

  joggle::Diagnostics old_interface_diagnostics;
  const auto old_interface =
      joggle::parse_module(R"(
    joggle 1;
    module old_interface@1.0.0 {
      interface type metric;
    }
  )",
                           old_interface_diagnostics, "old-interface.joggle");
  ok &= expect(!old_interface && !old_interface_diagnostics.ok(),
               "the pre-name interface subject syntax is not retained");

  joggle::Diagnostics wrong_interface_diagnostics;
  const auto wrong_interface = joggle::parse_module(R"(
    joggle 1;
    module wrong_interface@1.0.0 {
      interface elementwise: op;
      type scalar() : elementwise;
    }
  )",
                                                    wrong_interface_diagnostics,
                                                    "wrong-interface.joggle");
  ok &= expect(!wrong_interface && !wrong_interface_diagnostics.ok(),
               "an interface still constrains its declaration kind");

  joggle::Compiler pass_cycle;
  pass_cycle.add(R"(
    joggle 1;
    module pass_cycle@1.0.0 {
      pass first = second;
      pass second = first;
    }
  )",
                 "pass-cycle.joggle");
  const bool pass_cycle_linked = pass_cycle.link();
  const auto pass_cycle_diagnostics = pass_cycle.diagnostics().entries();
  ok &= expect(!pass_cycle_linked && !pass_cycle_diagnostics.empty() &&
                   pass_cycle_diagnostics.back().source &&
                   pass_cycle_diagnostics.back().source->source ==
                       "pass-cycle.joggle" &&
                   pass_cycle_diagnostics.back().source->begin.line == 5U,
               "pass composition cycle is rejected");

  joggle::Compiler missing;
  missing.add(R"(
    joggle 1;
    module app@1.0.0 {
      import absent@1;
    }
  )",
              "app.joggle");
  ok &= expect(!missing.link() && !missing.diagnostics().ok(),
               "missing import is diagnosed");

  joggle::Compiler invalid_contract;
  invalid_contract.add(R"(
    joggle 1;
    module base@1.0.0 {
      type scalar();
    }
  )",
                       "base.joggle");
  invalid_contract.add(R"(
    joggle 1;
    module invalid_contract@1.0.0 {
      import base@1 as b;
      op broken(input: b.missing) -> b.scalar;
    }
  )",
                       "invalid-contract.joggle");
  const bool contract_linked = invalid_contract.link();
  const auto contract_diagnostics = invalid_contract.diagnostics().entries();
  ok &= expect(!contract_linked && !contract_diagnostics.empty() &&
                   contract_diagnostics.back().source &&
                   contract_diagnostics.back().source->source ==
                       "invalid-contract.joggle" &&
                   contract_diagnostics.back().source->begin.line == 5U,
               "linking validates imported type expressions in op contracts");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
