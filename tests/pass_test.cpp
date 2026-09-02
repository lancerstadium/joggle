#include <cstdint>
#include <cstdlib>
#include <iostream>
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
  compiler.load(JOGGLE_BITMATH_MODULE);
  compiler.add(R"(
joggle 1;
module pipeline@1.0.0 {
  import bitmath@1;
  pass clean = bitmath.canonicalize;
}
)",
               "pipeline.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto bitmath = compiler.module("bitmath");
  const auto pipeline = compiler.module("pipeline");
  if (!bitmath || !compiler.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto word_decl = bitmath ? bitmath->type("word") : std::nullopt;
  const auto identity_decl =
      bitmath ? bitmath->operation("identity") : std::nullopt;
  const auto format_decl =
      bitmath ? bitmath->interface("numeric_format") : std::nullopt;
  const auto storage_bits =
      format_decl ? format_decl->method("storage_bits") : std::nullopt;
  const auto canonicalize =
      bitmath ? bitmath->pass("canonicalize") : std::nullopt;
  const auto clean = pipeline ? pipeline->pass("clean") : std::nullopt;
  if (!word_decl || !identity_decl || !storage_bits || !canonicalize ||
      !clean) {
    return EXIT_FAILURE;
  }
  const auto word = compiler.make(*word_decl, std::int64_t{8});
  auto graph = compiler.graph();
  if (!word || !graph) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  auto edit = graph->edit();
  const auto input = edit.argument(*word);
  const auto first = edit.append(*identity_decl, {input});
  const auto second = edit.append(*identity_decl, {first.result(0)});
  edit.append(*identity_decl, {second.result(0)});
  joggle::Diagnostics edit_diagnostics;
  if (!edit.commit(edit_diagnostics)) {
    edit_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto bits = compiler.call<std::int64_t>(*word, *storage_bits);
  ok &= expect(bits && *bits == 8,
               "the Bitmath behavior supplies interface methods");
  ok &= expect(canonicalize->form() == joggle::Module::PassDecl::Form::Rules,
               "the Module exposes one pass handle without a pattern API");
  ok &= expect(compiler.run(*graph, *clean),
               "an imported rule pass composes without a C++ binding");
  ok &= expect(graph->operations().empty(),
               "greedy contraction removes the complete identity chain");

  constexpr std::string_view repeated_source = R"(
joggle 1;
module repeated@1.0.0 {
  type value();
  op pair<T: type>(lhs: T, rhs: T) -> T;
  pass deduplicate {
    pair($x, $x) => $x;
  }
}
)";
  joggle::Compiler repeated_compiler;
  repeated_compiler.add(repeated_source, "repeated.joggle");
  if (!repeated_compiler.link()) {
    return EXIT_FAILURE;
  }
  const auto repeated = repeated_compiler.module("repeated");
  const auto value_decl = repeated ? repeated->type("value") : std::nullopt;
  const auto pair_decl = repeated ? repeated->operation("pair") : std::nullopt;
  const auto deduplicate =
      repeated ? repeated->pass("deduplicate") : std::nullopt;
  const auto value = value_decl ? repeated_compiler.make(*value_decl)
                                : std::optional<joggle::Type>{};
  auto repeated_graph = repeated_compiler.graph();
  if (!pair_decl || !deduplicate || !value || !repeated_graph) {
    return EXIT_FAILURE;
  }
  auto repeated_edit = repeated_graph->edit();
  const auto lhs = repeated_edit.argument(*value);
  const auto rhs = repeated_edit.argument(*value);
  repeated_edit.append(*pair_decl, {lhs, lhs});
  repeated_edit.append(*pair_decl, {lhs, rhs});
  joggle::Diagnostics repeated_diagnostics;
  if (!repeated_edit.commit(repeated_diagnostics)) {
    return EXIT_FAILURE;
  }
  ok &= expect(repeated_compiler.run(*repeated_graph, *deduplicate) &&
                   repeated_graph->operations().size() == 1U,
               "repeated pass variables require the same SSA value");

  constexpr std::string_view mismatch_source = R"(
    joggle 1;
    module mismatch@1.0.0 {
      type a();
      type b();
      op cast<A: type, B: type>(input: A) -> B;
      pass simplify {
        cast($input) => $input;
      }
    }
  )";
  joggle::Compiler mismatch_compiler;
  mismatch_compiler.add(mismatch_source, "mismatch.joggle");
  if (!mismatch_compiler.link()) {
    mismatch_compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto mismatch_module = mismatch_compiler.module("mismatch");
  const auto a_decl =
      mismatch_module ? mismatch_module->type("a") : std::nullopt;
  const auto b_decl =
      mismatch_module ? mismatch_module->type("b") : std::nullopt;
  const auto cast_decl =
      mismatch_module ? mismatch_module->operation("cast") : std::nullopt;
  const auto simplify =
      mismatch_module ? mismatch_module->pass("simplify") : std::nullopt;
  if (!a_decl || !b_decl || !cast_decl || !simplify) {
    return EXIT_FAILURE;
  }
  const auto a = mismatch_compiler.make(*a_decl);
  const auto b = mismatch_compiler.make(*b_decl);
  auto mismatch_graph = mismatch_compiler.graph();
  if (!a || !b || !mismatch_graph) {
    return EXIT_FAILURE;
  }
  auto mismatch_edit = mismatch_graph->edit();
  const auto mismatch_input = mismatch_edit.argument(*a);
  mismatch_edit.append(*cast_decl, {mismatch_input}, {*b});
  joggle::Diagnostics mismatch_edit_diagnostics;
  if (!mismatch_edit.commit(mismatch_edit_diagnostics)) {
    return EXIT_FAILURE;
  }
  ok &= expect(
      !mismatch_compiler.run(*mismatch_graph, *simplify) &&
          mismatch_graph->operations().size() == 1U &&
          mismatch_graph->operations().front().schema().name() ==
              "cast" &&
          mismatch_graph->operations().front().result(0).type() == *b,
      "type-incompatible rule fails and restores the whole Graph");

  constexpr std::string_view guarded_source = R"(
    joggle 1;
    module guarded@1.0.0 {
      type a();
      op identity<T: type>(input: T) -> T;
      pass touch;
    }
  )";
  joggle::Compiler guarded_compiler;
  guarded_compiler.add(guarded_source, "guarded.joggle");
  if (!guarded_compiler.link()) {
    return EXIT_FAILURE;
  }
  const auto guarded = guarded_compiler.module("guarded");
  const auto guarded_a_decl = guarded ? guarded->type("a") : std::nullopt;
  const auto guarded_identity =
      guarded ? guarded->operation("identity") : std::nullopt;
  const auto guarded_a =
      guarded_a_decl ? guarded_compiler.make(*guarded_a_decl) : std::nullopt;
  auto guarded_graph = guarded_compiler.graph();
  if (!guarded || !guarded_identity || !guarded_a || !guarded_graph) {
    return EXIT_FAILURE;
  }
  {
    auto edit = guarded_graph->edit();
    const auto input = edit.argument(*guarded_a);
    edit.append(*guarded_identity, {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  guarded_compiler.bind(
      *guarded_identity,
      [](const joggle::Operation&, joggle::Diagnostics& diagnostics) {
        diagnostics.report("guarded pass input rejected");
        return false;
      });
  bool pass_called = false;
  const auto guarded_touch = guarded->pass("touch");
  if (!guarded_touch) {
    return EXIT_FAILURE;
  }
  guarded_compiler.bind(
      *guarded_touch,
      [&](joggle::Compiler&, joggle::Graph&, joggle::Diagnostics&) {
        pass_called = true;
        return true;
      });
  ok &= expect(!guarded_compiler.run(*guarded_graph, "guarded.touch") &&
                   !pass_called && !guarded_compiler.ok(),
               "a pass does not execute on a Graph rejected by bound domain "
               "semantics");

  joggle::Compiler named_compiler;
  named_compiler.add("joggle 1; module named@1.0.0 { pass noop; }",
                     "named.joggle");
  const bool named_linked = named_compiler.link();
  const auto named_module = named_compiler.module("named");
  auto named_graph = named_compiler.graph();
  bool named_called = false;
  if (named_module) {
    const auto noop = named_module->pass("noop");
    if (!noop) {
      return EXIT_FAILURE;
    }
    named_compiler.bind(*noop, [&](joggle::Compiler&, joggle::Graph&,
                                   joggle::Diagnostics&) {
      named_called = true;
      return true;
    });
  }
  const bool unqualified_run =
      named_linked && named_graph && named_compiler.run(*named_graph, "noop");
  const auto named_diagnostics = named_compiler.diagnostics().entries();
  ok &=
      expect(!unqualified_run && !named_called && !named_diagnostics.empty() &&
                 named_diagnostics.back().message.find("module.member") !=
                     std::string::npos,
             "pass lookup requires one unambiguous qualified member name");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
