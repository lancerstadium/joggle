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
  std::ifstream input(JOGGLE_TEST_MOD);
  std::ostringstream text;
  text << input.rdbuf();

  joggle::Diag diagnostics;
  auto mod = joggle::parse_mod(text.str(), diagnostics, JOGGLE_TEST_MOD);
  if (!mod) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  ok &= expect(mod->name() == "test_ir", "mod name");
  ok &= expect(mod->digest().size() == 64U, "SHA-256 digest width");

  const auto rejects_language_version = [](std::uint32_t version) {
    joggle::Diag diagnostics;
    const auto parsed = joggle::parse_mod(
        "joggle " + std::to_string(version) + "; mod versioned@1.0.0 {}",
        diagnostics, "language-version.joggle");
    return !parsed && !diagnostics.ok() &&
           diagnostics.issues().front().message.find(
               "unsupported Joggle language version") != std::string::npos &&
           diagnostics.issues().front().source &&
           diagnostics.issues().front().source->begin.line == 1U;
  };
  ok &= expect(rejects_language_version(0U) && rejects_language_version(2U),
               "unknown language versions are rejected before Mod use");

  joggle::Diag legacy_graph_diagnostics;
  const auto legacy_graph = joggle::parse_mod(
      "joggle 1; mod legacy@1.0.0 { graph main() { return; } }",
      legacy_graph_diagnostics, "legacy-graph.joggle");
  ok &= expect(!legacy_graph && !legacy_graph_diagnostics.ok(),
               "graph is not a source-level member declaration");

  joggle::Diag legacy_op_diagnostics;
  const auto legacy_op = joggle::parse_mod(
      "joggle 1; mod legacy@1.0.0 { op add(lhs: i32) -> i32; }",
      legacy_op_diagnostics, "legacy-op.joggle");
  ok &= expect(!legacy_op && !legacy_op_diagnostics.ok(),
               "op is not a source-level member declaration");

  joggle::Diag legacy_pass_diagnostics;
  const auto legacy_pass =
      joggle::parse_mod("joggle 1; mod legacy@1.0.0 { pass optimize; }",
                        legacy_pass_diagnostics, "legacy-pass.joggle");
  ok &= expect(!legacy_pass && !legacy_pass_diagnostics.ok(),
               "pass is not a source-level member declaration");

  joggle::Diag operator_alias_diagnostics;
  const auto operator_alias =
      joggle::parse_mod("joggle 1; mod legacy@1.0.0 { "
                        "fn add(lhs: int, rhs: int) -> int as +; }",
                        operator_alias_diagnostics, "operator-alias.joggle");
  ok &= expect(!operator_alias && !operator_alias_diagnostics.ok(),
               "symbolic fns cannot use a second alias identity");

  const auto integer = mod->type("integer");
  const auto align = mod->fn("align");
  const auto add = mod->fn("+");
  const auto canonicalize = mod->fn("canonicalize");
  ok &=
      expect(integer && integer->parameters().size() == 2U &&
                 integer->derived_parameters().size() == 2U &&
                 integer->derived_parameters().front().name == "storage_bits" &&
                 integer->derived_parameters().front().value.kind ==
                     joggle::Mod::Expr::Kind::Variable &&
                 integer->derived_parameters().front().value.text == "width",
             "integer schema");
  ok &= expect(align && align->inputs().size() == 2U &&
                   align->results().size() == 1U &&
                   align->results().front().domain ==
                       joggle::Mod::Expr::reference("int") &&
                   align->form() == joggle::Mod::FnDecl::Form::Body &&
                   align->symbol().kind() == joggle::Mod::SymbolKind::Fn,
               "pure fn reflection");
  ok &= expect(
      add && add->inputs().size() == 2U && add->results().size() == 1U &&
          add->inputs().front().domain.kind ==
              joggle::Mod::Expr::Kind::Variable &&
          add->inputs().front().domain.text == "T" &&
          add->results().front().domain == add->inputs().front().domain &&
          add->signature().find("static:") == std::string::npos &&
          add->signature().find("value:") == std::string::npos &&
          add->name() == std::string_view("+") &&
          add->operator_fixity() == joggle::Mod::FnDecl::Fixity::Infix,
      "add signature");
  ok &= expect(canonicalize &&
                   canonicalize->form() == joggle::Mod::FnDecl::Form::External,
               "external compiler fns use ordinary declarations");
  if (!ok || !integer) {
    return EXIT_FAILURE;
  }

  const auto symbol = integer->symbol();
  ok &=
      expect(symbol.qualified_name() == "test_ir.integer", "qualified symbol");
  ok &= expect(symbol.stable_name().starts_with("test_ir@1.0.0/") &&
                   symbol.stable_name().ends_with("/type/integer"),
               "persistent symbol identity");

  const std::string canonical = joggle::format(*mod);
  ok &= expect(canonical == text.str(),
               "the compiler fixture is canonical source");
  joggle::Diag canonical_diagnostics;
  auto reparsed =
      joggle::parse_mod(canonical, canonical_diagnostics, "canonical.joggle");
  ok &= expect(reparsed && joggle::format(*reparsed) == canonical,
               "idempotent formatting");
  ok &= expect(reparsed && reparsed->digest() == mod->digest(),
               "canonical digest stability");
  const auto reparsed_integer =
      reparsed ? reparsed->type("integer") : std::nullopt;

  joggle::Diag surface_diagnostics;
  const auto surface = joggle::parse_mod(R"(
    joggle 1;
    mod surface@2.3.4 {
      fn empty() {
        return;
      }
      fn pipeline(input: fn) -> fn {
        return @(external(input));
      }
      fn external(input: fn) -> fn;
      fn collect<T>(items: T...) -> T;
      fn identity(value: int) -> int {
        return value;
      }
      type meta(values: list<int>, element: type);
      type scalar(
        bits: int = 8,
        scale: real = -1.5e2,
        signed: bool = true,
        label: string = "line\n\"quoted\""
      ) {
        shape: list<int> = [bits];
      }
      import dependency@^1.2.3 as dep;
    }
  )",
                                         surface_diagnostics, "surface.joggle");
  const std::string surface_text = surface ? joggle::format(*surface) : "";
  const auto position = [&](std::string_view text) {
    return surface_text.find(text);
  };
  const auto import_position = position("import dependency");
  const auto type_position = position("type scalar");
  const auto meta_position = position("type meta");
  const auto empty_position = position("fn empty");
  const auto pipeline_position = position("fn pipeline");
  const auto collect_position = position("fn collect");
  const auto identity_position = position("fn identity");
  joggle::Diag surface_roundtrip_diagnostics;
  const auto surface_roundtrip = joggle::parse_mod(
      surface_text, surface_roundtrip_diagnostics, "surface-canonical.joggle");
  ok &= expect(
      surface && import_position < meta_position &&
          meta_position < type_position && type_position < empty_position &&
          empty_position < pipeline_position &&
          pipeline_position < collect_position &&
          collect_position < identity_position && surface_roundtrip &&
          joggle::format(*surface_roundtrip) == surface_text &&
          surface_roundtrip->digest() == surface->digest(),
      "the complete DSL surface has one canonical member order and digest");

  joggle::Diag compiler_generic_diagnostics;
  const auto compiler_generic = joggle::parse_mod(
      R"(
    joggle 1;
    mod compiler_generic@1.0.0 {
      fn identity<N: int>(value: N) -> N {
        return value;
      }
    }
  )",
      compiler_generic_diagnostics, "compiler-generic.joggle");
  const auto compiler_identity =
      compiler_generic ? compiler_generic->fn("identity") : std::nullopt;
  const auto integer_domain = joggle::Mod::Expr::reference("int");
  ok &= expect(
      compiler_identity &&
          compiler_identity->inputs().front().domain == integer_domain &&
          compiler_identity->results().front().domain == integer_domain &&
          joggle::format(*compiler_generic)
                  .find("fn identity<N: int>(value: N) -> int") !=
              std::string::npos,
      "compiler-value generics bind inputs without creating a "
      "parallel port signature");

  joggle::Diag trivia_diagnostics;
  const std::string with_trivia =
      "\n# source location and comments are not identity\n" + text.str() +
      "\n# trailing repository note\n";
  const auto trivia_mod = joggle::parse_mod(
      with_trivia, trivia_diagnostics, "another/path/compiler_test.joggle");
  const auto trivia_integer =
      trivia_mod ? trivia_mod->type("integer") : std::nullopt;
  ok &= expect(trivia_mod && trivia_integer &&
                   trivia_mod->digest() == mod->digest() &&
                   joggle::format(*trivia_mod) == canonical &&
                   trivia_integer->symbol() == integer->symbol(),
               "whitespace, comments, and source paths do not affect "
               "canonical identity");

  joggle::Diag conflicting_identity_diagnostics;
  const auto conflicting_identity = joggle::parse_mod(
      R"(
    joggle 1;
    mod test_ir@1.0.0 {
      type integer(width: int);
    }
  )",
      conflicting_identity_diagnostics, "conflicting-identity.joggle");
  const auto conflicting_integer = conflicting_identity
                                       ? conflicting_identity->type("integer")
                                       : std::nullopt;
  ok &= expect(reparsed_integer && conflicting_integer &&
                   *reparsed_integer == *integer &&
                   *conflicting_integer == *integer &&
                   conflicting_identity->declaration_digest() !=
                       mod->declaration_digest(),
               "versioned declaration identity is stable while a declaration "
               "digest detects incompatible same-version Mods");

  joggle::Diag first_fn_diagnostics;
  const auto first_fn_mod =
      joggle::parse_mod(R"(
    joggle 1;
    mod fn_identity@1.0.0 {
      type value();
      fn identity<T>(input: T) -> T;
      fn main(input: value) -> value {
        return input;
      }
    }
  )",
                        first_fn_diagnostics, "first-fn.joggle");
  joggle::Diag second_fn_diagnostics;
  const auto second_fn_mod = joggle::parse_mod(
      R"(
    joggle 1;
    mod fn_identity@1.0.0 {
      type value();
      fn identity<T>(input: T) -> T;
      fn main(input: value) -> value {
        output = identity(input);
        return output;
      }
    }
  )",
      second_fn_diagnostics, "second-fn.joggle");
  const auto first_fn_symbol =
      first_fn_mod ? first_fn_mod->symbol(joggle::Mod::SymbolKind::Fn, "main")
                   : std::nullopt;
  const auto second_fn_symbol =
      second_fn_mod ? second_fn_mod->symbol(joggle::Mod::SymbolKind::Fn, "main")
                    : std::nullopt;
  ok &= expect(first_fn_mod && second_fn_mod && first_fn_symbol &&
                   second_fn_symbol &&
                   first_fn_mod->digest() != second_fn_mod->digest() &&
                   first_fn_mod->declaration_digest() ==
                       second_fn_mod->declaration_digest() &&
                   *first_fn_symbol == *second_fn_symbol,
               "fn body changes alter artifact identity without changing the "
               "declared surface");

  joggle::Diag list_diagnostics;
  auto list_mod = joggle::parse_mod(R"(
    joggle 1;
    mod shaped@1.0.0 {
      type tensor(element: type, shape: list<int>);
    }
  )",
                                    list_diagnostics, "shaped.joggle");
  const auto tensor = list_mod ? list_mod->type("tensor") : std::nullopt;
  ok &= expect(tensor && tensor->parameters().size() == 2U &&
                   tensor->parameters()[1].domain ==
                       joggle::Mod::Expr::list_domain(
                           joggle::Mod::Expr::reference("int")),
               "list-valued schema parameter");

  joggle::Diag numeric_diagnostics;
  auto numeric_mod = joggle::parse_mod(R"(
    joggle 1;
    mod numeric@1.2.3 {
      type scale(value: real = -1.5e2);
    }
  )",
                                       numeric_diagnostics, "numeric.joggle");
  const auto scale = numeric_mod ? numeric_mod->type("scale") : std::nullopt;
  ok &= expect(scale && scale->parameters().size() == 1U &&
                   scale->parameters().front().default_value &&
                   scale->parameters().front().default_value->kind ==
                       joggle::Mod::Expr::Kind::Number &&
                   scale->parameters().front().default_value->text == "-150",
               "semantic versions and numeric literals share one lexer");

  joggle::Compiler compiler;
  compiler.add(canonical, "compiler_test.joggle");
  compiler.add(R"(
    joggle 1;
    mod client@1.0.0 {
      import test_ir@1 as math;
      fn inference<T>(input: T) -> T;
      fn pipeline(input: fn) -> fn {
        return math.canonicalize(input);
      }
    }
  )",
               "client.joggle");
  ok &= expect(compiler.link(), "mod closure links");
  ok &= expect(compiler.linked() && compiler.mod("client").has_value(),
               "linked mod lookup");
  const auto linked_client = compiler.mod("client");
  ok &= expect(linked_client && linked_client->imports().size() == 1U &&
                   linked_client->imports().front().name == "test_ir" &&
                   linked_client->imports().front().prefix() == "math",
               "an import alias is a local prefix for one mod identity");
  const auto inference =
      linked_client ? linked_client->fn("inference") : std::nullopt;
  ok &= expect(inference.has_value(), "generic fns need no marker API");

  joggle::Diag duplicate_alias_diagnostics;
  const auto duplicate_alias =
      joggle::parse_mod(R"(
    joggle 1;
    mod aliases@1.0.0 {
      import first@1 as dep;
      import second@1 as dep;
    }
  )",
                        duplicate_alias_diagnostics, "duplicate-alias.joggle");
  ok &= expect(!duplicate_alias && !duplicate_alias_diagnostics.ok(),
               "duplicate import prefixes are rejected");

  joggle::Diag mod_alias_diagnostics;
  const auto mod_alias =
      joggle::parse_mod(R"(
    joggle 1;
    mod aliases@1.0.0 {
      import first@1 as aliases;
    }
  )",
                        mod_alias_diagnostics, "mod-alias.joggle");
  ok &= expect(!mod_alias && !mod_alias_diagnostics.ok(),
               "an import prefix cannot shadow its mod");

  joggle::Diag invalid_diagnostics;
  auto invalid =
      joggle::parse_mod("joggle 1; mod broken@1.0.0 { op add { inputs 2; } }",
                        invalid_diagnostics, "broken.joggle");
  ok &= expect(!invalid && !invalid_diagnostics.ok() &&
                   invalid_diagnostics.issues().front().source.has_value(),
               "old clause grammar is rejected with a source diagnostic");

  joggle::Diag removed_interface_diagnostics;
  auto removed_interface = joggle::parse_mod(R"(
    joggle 1;
    mod removed_interface@1.0.0 {
      interface marker: fn;
    }
  )",
                                             removed_interface_diagnostics,
                                             "removed-interface.joggle");
  ok &= expect(!removed_interface && !removed_interface_diagnostics.ok(),
               "interface is not a source-level declaration");

  joggle::Diag removed_attribute_diagnostics;
  auto removed_attribute = joggle::parse_mod(R"(
    joggle 1;
    mod removed_attribute@1.0.0 {
      attr metadata(value: int);
    }
  )",
                                             removed_attribute_diagnostics,
                                             "removed-attribute.joggle");
  ok &= expect(!removed_attribute && !removed_attribute_diagnostics.ok(),
               "attr is not a source-level declaration");

  joggle::Diag legacy_rewrite_diagnostics;
  auto legacy_rewrite =
      joggle::parse_mod(R"(
    joggle 1;
    mod legacy_rewrite@1.0.0 {
      type integer();
      fn identity<T>(input: T) -> T;
      fn simplify(input: fn) -> fn {
        return rewrite(input) {
          identity($input) => $input;
        };
      }
    }
  )",
                        legacy_rewrite_diagnostics, "legacy-rewrite.joggle");
  ok &= expect(!legacy_rewrite && !legacy_rewrite_diagnostics.ok(),
               "the removed core rewrite sublanguage is rejected");

  joggle::Diag removed_analysis_diagnostics;
  auto removed_analysis = joggle::parse_mod(
      R"(
    joggle 1;
    mod removed_analysis@1.0.0 {
      analysis widths;
    }
  )",
      removed_analysis_diagnostics, "removed-analysis.joggle");
  ok &= expect(!removed_analysis && !removed_analysis_diagnostics.ok(),
               "removed analysis declaration is rejected");

  joggle::Diag removed_require_diagnostics;
  auto removed_require = joggle::parse_mod(
      R"(
    joggle 1;
    mod removed_require@1.0.0 {
      require arith;
    }
  )",
      removed_require_diagnostics, "removed-require.joggle");
  ok &= expect(!removed_require && !removed_require_diagnostics.ok(),
               "removed require declaration is rejected");

  joggle::Diag value_name_diagnostics;
  const auto value_name =
      joggle::parse_mod(R"(
    joggle 1;
    mod value_name@1.0.0 {
      type value();
      fn identity(input: value) -> value;
    }
  )",
                        value_name_diagnostics, "value-name.joggle");
  ok &= expect(value_name && value_name_diagnostics.ok(),
               "value is an ordinary type name rather than dead syntax");

  joggle::Compiler fn_cycle;
  fn_cycle.add(R"(
    joggle 1;
    mod fn_cycle@1.0.0 {
      fn first(input: fn) -> fn {
        return second(input);
      }
      fn second(input: fn) -> fn {
        return first(input);
      }
    }
  )",
               "fn-cycle.joggle");
  const bool fn_cycle_linked = fn_cycle.link();
  const auto fn_cycle_diagnostics = fn_cycle.diag().issues();
  ok &= expect(!fn_cycle_linked && !fn_cycle_diagnostics.empty() &&
                   fn_cycle_diagnostics.back().source &&
                   fn_cycle_diagnostics.back().source->source ==
                       "fn-cycle.joggle" &&
                   fn_cycle_diagnostics.back().source->begin.line == 7U,
               "compiler-fn composition cycle is rejected");

  joggle::Compiler missing;
  missing.add(R"(
    joggle 1;
    mod app@1.0.0 {
      import absent@1;
    }
  )",
              "app.joggle");
  ok &= expect(!missing.link() && !missing.diag().ok(),
               "missing import is diagnosed");

  joggle::Compiler invalid_contract;
  invalid_contract.add(R"(
    joggle 1;
    mod base@1.0.0 {
      type scalar();
    }
  )",
                       "base.joggle");
  invalid_contract.add(R"(
    joggle 1;
    mod invalid_contract@1.0.0 {
      import base@1 as b;
      fn broken(input: b.missing) -> b.scalar;
    }
  )",
                       "invalid-contract.joggle");
  const bool contract_linked = invalid_contract.link();
  const auto contract_diagnostics = invalid_contract.diag().issues();
  ok &= expect(!contract_linked && !contract_diagnostics.empty() &&
                   contract_diagnostics.back().source &&
                   contract_diagnostics.back().source->source ==
                       "invalid-contract.joggle" &&
                   contract_diagnostics.back().source->begin.line == 5U,
               "linking validates imported type expressions in op contracts");

  joggle::Diag compiler_variadic_diagnostics;
  const auto compiler_variadic = joggle::parse_mod(
      R"(
    joggle 1;
    mod compiler_variadic@1.0.0 {
      fn invalid(values: int...) -> int;
    }
  )",
      compiler_variadic_diagnostics, "compiler-variadic.joggle");
  ok &= expect(!compiler_variadic && !compiler_variadic_diagnostics.ok(),
               "compiler collections use list domains instead of variadics");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
